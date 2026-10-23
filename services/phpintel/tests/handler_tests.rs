//! Response type tests for phpintel.

use rpc_common::types::RpcResponse;

#[test]
fn error_response_has_correct_code() {
    let resp = RpcResponse::err(
        Some(serde_json::json!(1)),
        -32601,
        "Method not found: $/unknown",
    );
    assert_eq!(resp.id, Some(serde_json::json!(1)));
    assert!(resp.error.is_some());
    let err = resp.error.unwrap();
    assert_eq!(err.code, -32601);
    assert!(err.message.contains("Method not found"));
}
