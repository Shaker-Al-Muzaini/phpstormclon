//! rpc-common — shared JSON-RPC 2.0 types and framing for Forge IDE services.
//!
//! Wire format: Content-Length: <n>\r\n\r\n<json body>
//! (identical to LSP 3.17 framing so editors can speak to phpintel directly).

pub mod codec;
pub mod types;

pub use types::{RpcError, RpcNotification, RpcRequest, RpcResponse};
