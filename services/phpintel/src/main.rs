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
use tokio_util::codec::{FramedRead, FramedWrite};
use tracing::{error, info, warn};

const VERSION: &str = env!("CARGO_PKG_VERSION");

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
                if let Err(e) = handle_frame(&mut writer, &frame).await {
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
) -> Result<()> {
    let req = match parse_request(frame) {
        Ok(req) => req,
        Err(response) => return send_response(writer, &response).await,
    };

    info!(method = %req.method, id = ?req.id, "Received request");
    let is_notification = req.id.is_none();

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
}
