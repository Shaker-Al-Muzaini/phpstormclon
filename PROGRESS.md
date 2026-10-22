# Forge IDE — Progress

## Status

**Current milestone: M0 — Foundations (verified locally)**
**Overall: M0 complete; M1 is next**

---

## Done

### M0 — Foundations (in progress)
- [x] Repository layout created (all directories from spec section 4)
- [x] `CMakeLists.txt` — top-level with Corrosion + Qt6 + vcpkg
- [x] `CMakePresets.json` — windows-debug and windows-release presets
- [x] `Cargo.toml` — workspace with all 6 service crates
- [x] `rust-toolchain.toml` — pinned to stable Rust
- [x] `vcpkg.json` — manifest for C++ dependencies
- [x] `core/` — ServiceSupervisor, RpcClient, ActionManager, Settings
- [x] `app/` — main.cpp, MainWindow (skeleton Qt window)
- [x] `ui/` — placeholder library (editor in M2)
- [x] `platform/` — Platform abstraction layer
- [x] `services/rpc-common` — JSON-RPC 2.0 framing + types
- [x] RPC framing tests — round-trip, partial bodies, multiple frames, invalid and case-insensitive headers
- [x] `services/phpintel` — skeleton with ping/shutdown/ready handlers + unit tests
- [x] `phpintel` stdio integration test — ready handshake, ping, unknown-method error, and clean shutdown
- [x] `services/vcs` — skeleton
- [x] `services/dbgp` — skeleton
- [x] `services/dbtools` — skeleton
- [x] `services/term` — skeleton
- [x] `services/ai-gateway` — skeleton
- [x] `docs/DECISIONS.md` — all version decisions recorded
- [x] `docs/ARCHITECTURE.md` — component diagrams
- [x] `.github/workflows/ci.yml` — Windows CI pipeline
- [x] `LICENSE`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md`
- [x] `resources/themes/` — Graphite and Daylight theme skeletons
- [x] `resources/keymaps/` — default (PhpStorm-compatible) and vscode keymaps
- [x] `tools/bootstrap.ps1` — developer setup script
- [x] Rust verification — `cargo build --all`, `cargo test --all`, `cargo fmt --all -- --check`, and `cargo clippy --all-targets --all-features -- -D warnings`
- [x] Windows C++/Qt verification — configured with Qt 6.8.3, built the full CMake/Cargo workspace, and passed both C++ smoke tests with CTest
- [x] Application smoke test — ForgeIDE remained running and started the `phpintel --stdio` child service
- [x] CI build/test steps reproduced locally — Rust and C++ checks pass; hosted GitHub Actions has not been run from this environment

## Known Issues / Gaps

- The app is still the M0 shell with a placeholder center panel; M1 docking, menus, themes, and settings UI are not implemented yet.
- Rust services other than `phpintel` and `rpc-common` remain skeletons; their product features have not been implemented.

## Next

### M1 — Shell and Docking
- Main window with Qt Advanced Docking System (ADS v5.1.1)
- Main menu, toolbar, status bar
- Tool window stripes (Project, Structure, Git, Terminal, etc.)
- Theme engine (Graphite, Daylight — JSON-driven)
- Action manager + keymap loading from JSON
- Settings dialog skeleton
- i18n infrastructure
- Layout save/restore per project

## Decisions Log
See `docs/DECISIONS.md`
