//! term — Terminal/PTY service (Milestone 6). M0: skeleton.
use anyhow::Result;
#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .json()
        .init();
    tracing::info!("term service starting (skeleton)");
    Ok(())
}
