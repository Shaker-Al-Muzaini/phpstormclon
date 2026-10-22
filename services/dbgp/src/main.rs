//! dbgp — Xdebug DBGp client service (Milestone 10). M0: skeleton.
use anyhow::Result;
#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .json()
        .init();
    tracing::info!("dbgp service starting (skeleton)");
    Ok(())
}
