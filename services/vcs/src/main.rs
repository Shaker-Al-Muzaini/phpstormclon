//! vcs — Forge IDE Git service (Milestone 7).
//! M0: skeleton only. Responds to $/ping.
mod handler;
use anyhow::Result;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt()
        .with_writer(std::io::stderr)
        .json()
        .init();
    tracing::info!("vcs service starting (skeleton)");
    // Full implementation in M7
    Ok(())
}
