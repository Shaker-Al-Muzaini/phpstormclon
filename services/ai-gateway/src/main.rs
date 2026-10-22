//! ai-gateway — Local AI assistant service (Milestone 12). M0: skeleton.
use anyhow::Result;
#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .json()
        .init();
    tracing::info!("ai-gateway service starting (skeleton)");
    Ok(())
}
