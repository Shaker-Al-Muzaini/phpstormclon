//! phpintel — Forge IDE PHP language analysis service.
//!
//! Protocol: JSON-RPC 2.0 over stdin/stdout with LSP-style framing.
//! Start with: phpintel --stdio
//!
//! Milestone 0: handshake only.
//!   - Responds to $/ping with { "pong": true, "version": "..." }
//!   - Responds to $/shutdown with clean exit
//!   - Emits a $/ready notification after startup

mod handler;

use anyhow::Result;
use bytes::BytesMut;
use futures_util::{SinkExt, StreamExt};
use rpc_common::{
    codec::LspFrameCodec,
    types::{RpcRequest, RpcResponse, error_codes},
};
use serde_json::Value;
use std::collections::HashMap;
use tokio_util::codec::{FramedRead, FramedWrite};
use tracing::{error, info, warn};
use tree_sitter::{InputEdit, Language, Parser, Point, Tree};

const VERSION: &str = env!("CARGO_PKG_VERSION");

struct PhpDocument {
    text: String,
    tree: Tree,
    version: Option<i64>,
}

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize structured logging to stderr (never stdout — stdout is the RPC channel)
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .with_env_filter("phpintel=debug,rpc_common=debug")
        .json()
        .init();

    info!("phpintel v{} starting", VERSION);

    let args: Vec<String> = std::env::args().collect();
    if !args.contains(&"--stdio".to_string()) {
        eprintln!("Usage: phpintel --stdio");
        eprintln!("phpintel v{} — Forge IDE PHP analysis service", VERSION);
        std::process::exit(1);
    }

    run_stdio_loop().await
}

async fn run_stdio_loop() -> Result<()> {
    let stdin = tokio::io::stdin();
    let stdout = tokio::io::stdout();

    let mut reader = FramedRead::new(stdin, LspFrameCodec);
    let mut writer = FramedWrite::new(stdout, LspFrameCodec);
    let mut documents = HashMap::<String, PhpDocument>::new();

    // Emit $/ready notification
    send_notification(
        &mut writer,
        "$/ready",
        serde_json::json!({
            "service": "phpintel",
            "version": VERSION,
        }),
    )
    .await?;

    info!("phpintel ready, entering message loop");

    loop {
        match reader.next().await {
            None => {
                info!("stdin closed, shutting down");
                break;
            }
            Some(Err(e)) => {
                error!("Framing error: {}", e);
                break;
            }
            Some(Ok(frame)) => {
                if let Err(e) = handle_frame(&mut writer, &frame, &mut documents).await {
                    error!("Handler error: {}", e);
                }
            }
        }
    }

    info!("phpintel exiting cleanly");
    Ok(())
}

async fn handle_frame(
    writer: &mut FramedWrite<tokio::io::Stdout, LspFrameCodec>,
    frame: &BytesMut,
    documents: &mut HashMap<String, PhpDocument>,
) -> Result<()> {
    let req = match parse_request(frame) {
        Ok(req) => req,
        Err(response) => return send_response(writer, &response).await,
    };

    info!(method = %req.method, id = ?req.id, "Received request");
    let is_notification = req.id.is_none();

    if is_notification
        && matches!(
            req.method.as_str(),
            "textDocument/didOpen" | "textDocument/didChange" | "textDocument/didClose"
        )
    {
        handle_document_notification(writer, &req.method, req.params.as_ref(), documents).await?;
        return Ok(());
    }

    let response = match req.method.as_str() {
        "$/ping" => handler::ping(&req),
        "$/shutdown" => {
            if let Some(id) = req.id.clone() {
                let resp = RpcResponse::ok(Some(id), serde_json::json!({ "ok": true }))?;
                send_response(writer, &resp).await?;
            }
            info!("Shutdown requested, exiting");
            std::process::exit(0);
        }
        other => {
            warn!("Unknown method: {}", other);
            RpcResponse::err(
                req.id.clone(),
                error_codes::METHOD_NOT_FOUND,
                format!("Method not found: {}", other),
            )
        }
    };

    if is_notification {
        Ok(())
    } else {
        send_response(writer, &response).await
    }
}

async fn handle_document_notification(
    writer: &mut FramedWrite<tokio::io::Stdout, LspFrameCodec>,
    method: &str,
    params: Option<&Value>,
    documents: &mut HashMap<String, PhpDocument>,
) -> Result<()> {
    let Some(params) = params.and_then(Value::as_object) else {
        warn!(
            method,
            "Ignoring document notification without an object params value"
        );
        return Ok(());
    };
    let Some(document) = params.get("textDocument").and_then(Value::as_object) else {
        warn!(
            method,
            "Ignoring document notification without textDocument"
        );
        return Ok(());
    };
    let Some(uri) = document.get("uri").and_then(Value::as_str) else {
        warn!(
            method,
            "Ignoring document notification without a document URI"
        );
        return Ok(());
    };

    match method {
        "textDocument/didOpen" => {
            let Some(text) = document.get("text").and_then(Value::as_str) else {
                warn!(uri, "Ignoring didOpen notification without document text");
                return Ok(());
            };
            let mut parser = Parser::new();
            let language: Language = tree_sitter_php::LANGUAGE_PHP.into();
            if let Err(error) = parser.set_language(&language) {
                warn!(%error, "Could not configure PHP parser");
                return Ok(());
            }
            let Some(tree) = parser.parse(text, None) else {
                warn!(uri, "PHP parser did not return a syntax tree");
                return Ok(());
            };
            let version = document.get("version").and_then(Value::as_i64);
            let diagnostics = collect_diagnostics(text, &tree);
            documents.insert(
                uri.to_owned(),
                PhpDocument {
                    text: text.to_owned(),
                    tree,
                    version,
                },
            );
            publish_diagnostics(writer, uri, version, diagnostics).await?;
        }
        "textDocument/didChange" => {
            let Some(existing) = documents.get_mut(uri) else {
                warn!(uri, "Ignoring didChange for a document that is not open");
                return Ok(());
            };
            let Some(text) = params
                .get("contentChanges")
                .and_then(Value::as_array)
                .and_then(|changes| changes.last())
                .and_then(|change| change.get("text"))
                .and_then(Value::as_str)
            else {
                warn!(uri, "Ignoring didChange without full document text");
                return Ok(());
            };
            let version = document.get("version").and_then(Value::as_i64);
            if existing
                .version
                .zip(version)
                .is_some_and(|(old, new)| new <= old)
            {
                warn!(uri, "Ignoring stale document version");
                return Ok(());
            }
            incremental_edit(&mut existing.tree, &existing.text, text);
            let mut parser = Parser::new();
            let language: Language = tree_sitter_php::LANGUAGE_PHP.into();
            if let Err(error) = parser.set_language(&language) {
                warn!(%error, "Could not configure PHP parser");
                return Ok(());
            }
            let Some(tree) = parser.parse(text, Some(&existing.tree)) else {
                warn!(uri, "PHP parser did not return an updated syntax tree");
                return Ok(());
            };
            existing.text = text.to_owned();
            existing.tree = tree;
            existing.version = version;
            let diagnostics = collect_diagnostics(text, &existing.tree);
            publish_diagnostics(writer, uri, version, diagnostics).await?;
        }
        "textDocument/didClose" => {
            documents.remove(uri);
            publish_diagnostics(writer, uri, None, Vec::new()).await?;
        }
        _ => {}
    }
    Ok(())
}

#[derive(Debug)]
struct PhpDiagnostic {
    start_line: usize,
    start_character: usize,
    end_line: usize,
    end_character: usize,
    code: &'static str,
    message: &'static str,
}

fn collect_diagnostics(source: &str, tree: &Tree) -> Vec<PhpDiagnostic> {
    let mut diagnostics = Vec::new();
    let mut nodes = vec![tree.root_node()];
    while let Some(node) = nodes.pop() {
        if node.is_missing() || node.is_error() {
            let (start_line, start_character, end_line, end_character, code, message) =
                if node.is_missing() && node.kind() == ";" {
                    let position = previous_token_end(source, node);
                    (
                        position.row,
                        utf16_column(source, position),
                        position.row,
                        utf16_column(source, position),
                        "php.syntax.missing-semicolon",
                        "Expected ';' after this statement",
                    )
                } else {
                    let start = node.start_position();
                    let mut end = node.end_position();
                    if end <= start {
                        end = start;
                    }
                    (
                        start.row,
                        utf16_column(source, start),
                        end.row,
                        utf16_column(source, end),
                        "php.syntax",
                        "PHP syntax error",
                    )
                };
            diagnostics.push(PhpDiagnostic {
                start_line,
                start_character,
                end_line,
                end_character,
                code,
                message,
            });
        }
        let mut cursor = node.walk();
        for child in node.children(&mut cursor) {
            nodes.push(child);
        }
    }
    diagnostics.sort_by_key(|diagnostic| {
        (
            diagnostic.start_line,
            diagnostic.start_character,
            diagnostic.code,
        )
    });
    diagnostics.dedup_by(|left, right| {
        left.start_line == right.start_line
            && left.start_character == right.start_character
            && left.code == right.code
    });
    diagnostics
}

fn incremental_edit(tree: &mut Tree, old_text: &str, new_text: &str) {
    let old_bytes = old_text.as_bytes();
    let new_bytes = new_text.as_bytes();
    let mut start_byte = old_bytes
        .iter()
        .zip(new_bytes)
        .take_while(|(old, new)| old == new)
        .count();
    while !old_text.is_char_boundary(start_byte) || !new_text.is_char_boundary(start_byte) {
        start_byte -= 1;
    }
    let mut old_end_byte = old_bytes.len();
    let mut new_end_byte = new_bytes.len();
    while old_end_byte > start_byte
        && new_end_byte > start_byte
        && old_bytes[old_end_byte - 1] == new_bytes[new_end_byte - 1]
    {
        old_end_byte -= 1;
        new_end_byte -= 1;
    }
    while !old_text.is_char_boundary(old_end_byte) {
        old_end_byte += 1;
    }
    while !new_text.is_char_boundary(new_end_byte) {
        new_end_byte += 1;
    }

    tree.edit(&InputEdit {
        start_byte,
        old_end_byte,
        new_end_byte,
        start_position: point_at_byte(old_text, start_byte),
        old_end_position: point_at_byte(old_text, old_end_byte),
        new_end_position: point_at_byte(new_text, new_end_byte),
    });
}

fn point_at_byte(source: &str, byte_offset: usize) -> Point {
    let prefix = &source[..byte_offset];
    let row = prefix.bytes().filter(|byte| *byte == b'\n').count();
    let column = prefix
        .rsplit_once('\n')
        .map_or(prefix.len(), |(_, line)| line.len());
    Point { row, column }
}

fn previous_token_end(source: &str, node: tree_sitter::Node<'_>) -> Point {
    let mut previous = node.prev_sibling();
    while let Some(candidate) = previous {
        if !candidate.is_extra() && !candidate.is_missing() {
            return candidate.end_position();
        }
        previous = candidate.prev_sibling();
    }
    let byte_offset = node.start_byte();
    let prefix = &source[..byte_offset.min(source.len())];
    let trimmed = prefix.trim_end();
    point_at_byte(source, trimmed.len())
}

fn utf16_column(source: &str, point: Point) -> usize {
    source.lines().nth(point.row).map_or(point.column, |line| {
        let byte_column = point.column.min(line.len());
        line[..byte_column].encode_utf16().count()
    })
}

async fn publish_diagnostics(
    writer: &mut FramedWrite<tokio::io::Stdout, LspFrameCodec>,
    uri: &str,
    version: Option<i64>,
    diagnostics: Vec<PhpDiagnostic>,
) -> Result<()> {
    let items = diagnostics
        .into_iter()
        .map(|diagnostic| {
            serde_json::json!({
                "range": {
                    "start": {
                        "line": diagnostic.start_line,
                        "character": diagnostic.start_character
                    },
                    "end": {
                        "line": diagnostic.end_line,
                        "character": diagnostic.end_character
                    }
                },
                "severity": 1,
                "code": diagnostic.code,
                "source": "phpintel",
                "message": diagnostic.message
            })
        })
        .collect::<Vec<_>>();
    let mut params = serde_json::json!({"uri": uri, "diagnostics": items});
    if let Some(version) = version {
        params["version"] = serde_json::json!(version);
    }
    let notification = serde_json::json!({
        "jsonrpc": "2.0",
        "method": "textDocument/publishDiagnostics",
        "params": params
    });
    writer.send(serde_json::to_vec(&notification)?).await?;
    Ok(())
}

fn parse_request(frame: &[u8]) -> Result<RpcRequest, Box<RpcResponse>> {
    let value: Value = serde_json::from_slice(frame).map_err(|error| {
        warn!("JSON parse error: {}", error);
        Box::new(RpcResponse::err(
            Some(Value::Null),
            error_codes::PARSE_ERROR,
            "Parse error",
        ))
    })?;
    if value
        .as_object()
        .is_some_and(|object| object.get("id").is_some_and(Value::is_null))
    {
        return Err(Box::new(RpcResponse::err(
            Some(Value::Null),
            error_codes::INVALID_REQUEST,
            "Invalid Request",
        )));
    }
    let req: RpcRequest = serde_json::from_value(value).map_err(|error| {
        warn!("Invalid JSON-RPC request: {}", error);
        Box::new(RpcResponse::err(
            Some(Value::Null),
            error_codes::INVALID_REQUEST,
            "Invalid Request",
        ))
    })?;

    if req.jsonrpc != "2.0"
        || req
            .id
            .as_ref()
            .is_some_and(|id| !matches!(id, Value::String(_) | Value::Number(_)))
    {
        return Err(Box::new(RpcResponse::err(
            Some(Value::Null),
            error_codes::INVALID_REQUEST,
            "Invalid Request",
        )));
    }

    Ok(req)
}

async fn send_response(
    writer: &mut FramedWrite<tokio::io::Stdout, LspFrameCodec>,
    resp: &RpcResponse,
) -> Result<()> {
    let json = serde_json::to_vec(resp)?;
    writer.send(json).await?;
    Ok(())
}

async fn send_notification(
    writer: &mut FramedWrite<tokio::io::Stdout, LspFrameCodec>,
    method: &str,
    params: Value,
) -> Result<()> {
    let notif = serde_json::json!({
        "jsonrpc": "2.0",
        "method": method,
        "params": params,
    });
    let json = serde_json::to_vec(&notif)?;
    writer.send(json).await?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_request_with_integer_id() {
        let request = parse_request(br#"{"jsonrpc":"2.0","id":7,"method":"$/ping"}"#).unwrap();

        assert_eq!(request.id, Some(serde_json::json!(7)));
        assert_eq!(request.method, "$/ping");
    }

    #[test]
    fn parses_notification_without_id() {
        let request = parse_request(br#"{"jsonrpc":"2.0","method":"$/ready"}"#).unwrap();

        assert!(request.id.is_none());
    }

    #[test]
    fn rejects_null_request_id() {
        let response =
            parse_request(br#"{"jsonrpc":"2.0","id":null,"method":"$/ping"}"#).unwrap_err();

        assert_eq!(response.error.unwrap().code, error_codes::INVALID_REQUEST);
    }

    #[test]
    fn rejects_boolean_request_id() {
        let response =
            parse_request(br#"{"jsonrpc":"2.0","id":true,"method":"$/ping"}"#).unwrap_err();

        assert_eq!(response.error.unwrap().code, error_codes::INVALID_REQUEST);
    }

    #[test]
    fn missing_semicolon_is_reported_at_previous_line_end() {
        let source = "<?php\nnamespace App\nuse Example\\Thing;\n";
        let mut parser = Parser::new();
        let language: Language = tree_sitter_php::LANGUAGE_PHP.into();
        parser
            .set_language(&language)
            .expect("PHP grammar is valid");
        let tree = parser
            .parse(source, None)
            .expect("PHP parser returns a tree");
        let diagnostics = collect_diagnostics(source, &tree);
        let missing_semicolon = diagnostics
            .iter()
            .find(|diagnostic| diagnostic.code == "php.syntax.missing-semicolon")
            .expect("a missing semicolon diagnostic is reported");
        assert_eq!(missing_semicolon.start_line, 1);
        assert_eq!(missing_semicolon.start_character, "namespace App".len());
    }

    #[test]
    fn incremental_edit_updates_php_diagnostics_and_clears_them_after_fix() {
        let mut parser = Parser::new();
        let language: Language = tree_sitter_php::LANGUAGE_PHP.into();
        parser
            .set_language(&language)
            .expect("PHP grammar is valid");
        let original = "<?php\nnamespace App\n";
        let mut tree = parser
            .parse(original, None)
            .expect("PHP parser returns a tree");
        assert!(!collect_diagnostics(original, &tree).is_empty());

        let fixed = "<?php\nnamespace App;\n";
        incremental_edit(&mut tree, original, fixed);
        tree = parser
            .parse(fixed, Some(&tree))
            .expect("incremental parse succeeds");
        assert!(collect_diagnostics(fixed, &tree).is_empty());
    }

    #[test]
    fn missing_semicolon_uses_utf16_and_ignores_trailing_comment() {
        let source = "<?php\nnamespace تجريبي // comment\nuse Example\\Thing;\n";
        let mut parser = Parser::new();
        parser
            .set_language(&tree_sitter_php::LANGUAGE_PHP.into())
            .unwrap();
        let tree = parser.parse(source, None).unwrap();
        let diagnostics = collect_diagnostics(source, &tree);
        let diagnostic = diagnostics
            .iter()
            .find(|item| item.code == "php.syntax.missing-semicolon")
            .unwrap();
        let expected = "namespace تجريبي".encode_utf16().count();
        assert_eq!(diagnostic.start_line, 1);
        assert_eq!(diagnostic.start_character, expected);
        assert_eq!(diagnostic.end_character, expected);
    }
}
