| Component | License | Source | Notes |
|-----------|---------|--------|-------|
| Qt 6.8 | LGPL v3 | qt.io | Dynamically linked. Qt DLLs shipped. Relinking supported. |
| Qt Advanced Docking System | LGPL v2.1 | github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | Dynamically linked. |
| JetBrains Mono font | SIL OFL 1.1 | github.com/JetBrains/JetBrainsMono | Bundled. |
| Inter font | SIL OFL 1.1 | rsms.me/inter | Bundled. |
| Lucide icons | ISC | github.com/lucide-icons/lucide | SVG icons bundled. |
| Rust standard library | MIT / Apache-2.0 | rust-lang.org | Statically linked into service binaries. |
| tokio | MIT | crates.io/crates/tokio | Statically linked. |
| serde / serde_json | MIT / Apache-2.0 | crates.io/crates/serde | Statically linked. |
| tokio-util | MIT | crates.io/crates/tokio-util | Statically linked. |
| bytes | MIT | crates.io/crates/bytes | Statically linked. |
| futures-util | MIT / Apache-2.0 | crates.io/crates/futures | Statically linked. |
| tree-sitter | MIT | crates.io/crates/tree-sitter | Statically linked. |
| tracing | MIT | crates.io/crates/tracing | Statically linked. |
| tracing-subscriber | MIT | crates.io/crates/tracing-subscriber | Statically linked. |
| anyhow | MIT / Apache-2.0 | crates.io/crates/anyhow | Statically linked. |
| thiserror | MIT / Apache-2.0 | crates.io/crates/thiserror | Statically linked. |
| uuid | Apache-2.0 | crates.io/crates/uuid | Statically linked. |
| git2 | MIT | crates.io/crates/git2 | vcs service only. libgit2 linked. |
| libgit2 | GPL-2.0 with linking exception | libgit2.org | Dynamically linked via git2-sys. |
| sqlx | MIT / Apache-2.0 | crates.io/crates/sqlx | dbtools service only. |
| portable-pty | MIT | crates.io/crates/portable-pty | term service only. |
| reqwest | MIT / Apache-2.0 | crates.io/crates/reqwest | ai-gateway service only. |
| nlohmann/json | MIT | github.com/nlohmann/json | C++ header-only. |
| spdlog | MIT | github.com/gabime/spdlog | C++ logging. |
| GoogleTest | BSD-3-Clause | github.com/google/googletest | Test binaries only, not shipped. |

This file is maintained manually. Run `cargo deny check licenses` for Rust license verification.
CI regenerates this file on each release build.
