//! Request handlers for phpintel.

use rpc_common::types::RpcResponse;

const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Handle $/ping — returns { pong: true, version: "x.y.z" }
pub fn ping(req: &rpc_common::types::RpcRequest) -> RpcResponse {
    RpcResponse::ok(
        req.id,
        serde_json::json!({
            "pong": true,
            "version": VERSION,
            "service": "phpintel",
        }),
    )
    .unwrap_or_else(|e| RpcResponse::err(req.id, -32603, e.to_string()))
}

#[cfg(test)]
mod tests {
    use super::*;
    use rpc_common::types::RpcRequest;

    fn make_ping_req(id: i64) -> RpcRequest {
        RpcRequest {
            jsonrpc: "2.0".into(),
            id,
            method: "$/ping".into(),
            params: None,
        }
    }

    #[test]
    fn ping_returns_pong() {
        let req = make_ping_req(42);
        let resp = ping(&req);
        assert_eq!(resp.id, 42);
        assert!(resp.error.is_none());
        let result = resp.result.unwrap();
        assert_eq!(result["pong"], true);
        assert_eq!(result["service"], "phpintel");
    }

    #[test]
    fn ping_preserves_id() {
        for id in [0, 1, 99, i64::MAX] {
            let req = make_ping_req(id);
            assert_eq!(ping(&req).id, id);
        }
    }
}
