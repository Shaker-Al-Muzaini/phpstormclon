//! dbtools — Database tools service (Milestone 11). M0: skeleton.
use anyhow::Result;
#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .json()
        .init();
    tracing::info!("dbtools service starting (skeleton)");
    Ok(())
}
