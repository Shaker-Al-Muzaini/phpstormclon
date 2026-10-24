//! End-to-end test for the phpintel stdio JSON-RPC handshake.

use std::{io, time::Duration};
use tokio::{
    io::{AsyncBufReadExt, AsyncReadExt, AsyncWriteExt, BufReader},
    process::{ChildStdin, ChildStdout, Command},
    time::timeout,
};

const IO_TIMEOUT: Duration = Duration::from_secs(5);

async fn read_message(stdout: &mut BufReader<ChildStdout>) -> io::Result<serde_json::Value> {
    let mut content_length = None;
    loop {
        let mut line = String::new();
        if stdout.read_line(&mut line).await? == 0 {
            return Err(io::Error::new(
                io::ErrorKind::UnexpectedEof,
                "service closed stdout before completing a frame",
            ));
        }
        if line == "\r\n" || line == "\n" {
            break;
        }
        if let Some((name, value)) = line.trim_end().split_once(':')
            && name.trim().eq_ignore_ascii_case("content-length")
        {
            content_length = Some(
                value
                    .trim()
                    .parse::<usize>()
                    .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?,
            );
        }
    }

    let content_length = content_length.ok_or_else(|| {
        io::Error::new(io::ErrorKind::InvalidData, "missing Content-Length header")
    })?;
    let mut body = vec![0; content_length];
    stdout.read_exact(&mut body).await?;
    serde_json::from_slice(&body).map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))
}

async fn write_message(stdin: &mut ChildStdin, message: &serde_json::Value) -> io::Result<()> {
    let body = serde_json::to_vec(message)
        .map_err(|error| io::Error::new(io::ErrorKind::InvalidData, error))?;
    write_raw_message(stdin, &body).await
}

async fn write_raw_message(stdin: &mut ChildStdin, body: &[u8]) -> io::Result<()> {
    stdin
        .write_all(format!("Content-Length: {}\r\n\r\n", body.len()).as_bytes())
        .await?;
    stdin.write_all(body).await?;
    stdin.flush().await
}

async fn read_message_with_timeout(stdout: &mut BufReader<ChildStdout>) -> serde_json::Value {
    timeout(IO_TIMEOUT, read_message(stdout))
        .await
        .expect("timed out waiting for phpintel response")
        .expect("invalid JSON-RPC frame from phpintel")
}

#[tokio::test]
async fn phpintel_handshakes_answers_requests_and_shuts_down() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_phpintel"))
        .arg("--stdio")
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .kill_on_drop(true)
        .spawn()
        .expect("failed to start phpintel");
    let mut stdin = child.stdin.take().expect("phpintel stdin is unavailable");
    let mut stdout = BufReader::new(child.stdout.take().expect("phpintel stdout is unavailable"));

    let ready = read_message_with_timeout(&mut stdout).await;
    assert_eq!(ready["method"], "$/ready");
    assert_eq!(ready["params"]["service"], "phpintel");

    write_raw_message(&mut stdin, b"{\"jsonrpc\":")
        .await
        .unwrap();
    let parse_error = read_message_with_timeout(&mut stdout).await;
    assert_eq!(parse_error["id"], serde_json::Value::Null);
    assert_eq!(parse_error["error"]["code"], -32700);

    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "id": true, "method": "$/ping"}),
    )
    .await
    .expect("failed to send invalid request");
    let invalid_request = read_message_with_timeout(&mut stdout).await;
    assert_eq!(invalid_request["id"], serde_json::Value::Null);
    assert_eq!(invalid_request["error"]["code"], -32600);

    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "method": "$/unknown"}),
    )
    .await
    .expect("failed to send notification");
    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "id": "ping-1", "method": "$/ping"}),
    )
    .await
    .expect("failed to send ping");
    let pong = read_message_with_timeout(&mut stdout).await;
    assert_eq!(pong["id"], "ping-1");
    assert_eq!(pong["result"]["pong"], true);
    assert_eq!(pong["result"]["service"], "phpintel");

    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "id": 2, "method": "$/unknown"}),
    )
    .await
    .expect("failed to send unknown method request");
    let error = read_message_with_timeout(&mut stdout).await;
    assert_eq!(error["id"], 2);
    assert_eq!(error["error"]["code"], -32601);

    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "id": 3, "method": "$/shutdown"}),
    )
    .await
    .expect("failed to send shutdown request");
    let shutdown = read_message_with_timeout(&mut stdout).await;
    assert_eq!(shutdown["id"], 3);
    assert_eq!(shutdown["result"]["ok"], true);

    let status = timeout(IO_TIMEOUT, child.wait())
        .await
        .expect("phpintel did not exit after shutdown")
        .expect("failed to wait for phpintel");
    assert!(status.success());
}

#[tokio::test]
async fn phpintel_publishes_and_clears_incremental_php_diagnostics() {
    let mut child = Command::new(env!("CARGO_BIN_EXE_phpintel"))
        .arg("--stdio")
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::null())
        .kill_on_drop(true)
        .spawn()
        .expect("failed to start phpintel");
    let mut stdin = child.stdin.take().expect("phpintel stdin is unavailable");
    let mut stdout = BufReader::new(child.stdout.take().expect("phpintel stdout is unavailable"));
    let ready = read_message_with_timeout(&mut stdout).await;
    assert_eq!(ready["method"], "$/ready");

    write_message(
        &mut stdin,
        &serde_json::json!({
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file:///workspace/example.php",
                    "languageId": "php",
                    "version": 1,
                    "text": "<?php\nnamespace App\nuse Example\\Thing;\n"
                }
            }
        }),
    )
    .await
    .expect("failed to send didOpen notification");
    let published = read_message_with_timeout(&mut stdout).await;
    assert_eq!(published["method"], "textDocument/publishDiagnostics");
    let diagnostics = published["params"]["diagnostics"]
        .as_array()
        .expect("diagnostics is an array");
    let missing_semicolon = diagnostics
        .iter()
        .find(|diagnostic| diagnostic["code"] == "php.syntax.missing-semicolon")
        .expect("missing semicolon is reported");
    assert_eq!(missing_semicolon["range"]["start"]["line"], 1);
    assert_eq!(missing_semicolon["source"], "phpintel");

    write_message(
        &mut stdin,
        &serde_json::json!({
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {
                    "uri": "file:///workspace/example.php",
                    "version": 2
                },
                "contentChanges": [{"text": "<?php\nnamespace App;\nuse Example\\Thing;\n"}]
            }
        }),
    )
    .await
    .expect("failed to send didChange notification");
    let cleared = read_message_with_timeout(&mut stdout).await;
    assert_eq!(
        cleared["params"]["diagnostics"]
            .as_array()
            .expect("published diagnostics is an array")
            .len(),
        0
    );

    write_message(
        &mut stdin,
        &serde_json::json!({
            "jsonrpc": "2.0", "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": "file:///workspace/example.php", "version": 1},
                "contentChanges": [{"text": "<?php namespace App"}]
            }
        }),
    )
    .await
    .expect("failed to send stale document version");
    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "id": 99, "method": "$/ping"}),
    )
    .await
    .unwrap();
    let response = read_message_with_timeout(&mut stdout).await;
    assert_eq!(
        response["id"], 99,
        "a stale edit must not publish diagnostics"
    );
    assert_eq!(response["result"]["pong"], true);

    write_message(
        &mut stdin,
        &serde_json::json!({"jsonrpc": "2.0", "method": "$/shutdown"}),
    )
    .await
    .expect("failed to send shutdown notification");
    timeout(IO_TIMEOUT, child.wait())
        .await
        .expect("phpintel did not shut down")
        .expect("failed to wait for phpintel");
}
