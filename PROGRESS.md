# Forge IDE — Progress

## Status

**Current milestone: M1 — Shell and Docking (in progress)**
**Overall: M0 complete; initial M1 shell implemented and verified locally**

---

## Done

### M0 — Foundations (complete)
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

### M1 — Shell and Docking (in progress)
- [x] Main window menus, toolbar, and service status bar
- [x] Project and Terminal tool windows using Qt dock widgets; placeholder Structure,
  Assistant, Problems, and Run panels are omitted from the current workspace
- [x] Graphite and Daylight theme switching from the bundled JSON resources
- [x] Default JSON keymap loaded through the ActionManager
- [x] Settings dialog for theme selection
- [x] Window geometry, dock layout, and last project path persistence
- [x] Project folder browser and basic text-file open/edit/save/close workflow
- [x] Basic integrated Windows PowerShell session with project working directory and command I/O;
  common commands and aliases such as `ls` work
- [x] Shortcut actions use application-wide Qt dispatch; child-widget dispatch has a unit test, but live keyboard input still needs end-to-end verification
- [x] Current-file Find/Find Next/Go to Line actions and key bindings
- [x] Run saved PHP files when PHP is installed; show Git status when Git is installed
- [x] Original vector icons for common toolbar/menu actions, project folders and PHP/Blade/web/config files, welcome buttons, and the app window; colors follow Graphite/Daylight
- [x] Project file hover identifies common source/config languages; Go to File and Recent Files navigation actions
- [x] Terminal command history (Up/Down), clear, copy, and paste shortcuts scoped to the terminal
- [x] Compact workspace layout with project tree, central editor, one terminal dock, and
  caret/language indicators
- [x] Source editor preview with line numbers, active-line highlighting, theme-aware fonts,
  basic PHP function folding, and lexical syntax colors for common PHP/Blade, web, Python, SQL,
  JSON, and YAML files
- [x] Plugin Manager entry with local manifest discovery, plugin folder access, and clear
  Marketplace/language-support status
- [x] Xdebug 3 CLI session start on the local loopback listener, caret-line breakpoint,
  basic step commands, and Cachegrind self-cost summary after profiling
- [x] Rebuilt Windows application; CTest (5/5) and Rust workspace tests (9/9) passed
- [x] Launched the IDE and verified the `phpintel.exe` service process starts alongside it

## Known Issues / Gaps

- M1 is not complete: tool windows use Qt's built-in docking rather than Qt Advanced Docking System; configurable auto-hide stripes, working debugger controls, complete settings/keymap selection, per-project layouts, and RTL/high-DPI verification remain.
- The icon system assigns language badges to common PHP/Blade/web/config file types; unsupported extensions use the generic document icon.
- The current terminal is a redirected PowerShell session, not ConPTY. Full terminal emulation, interactive TTY applications, terminal profiles/tabs, and robust Ctrl+C handling remain planned for M6.
- The current editor has a gutter and basic lexical highlighting, but not tree-sitter parsing or semantic tokens. Encoding detection, line-ending preservation, tabs/splits, folding, and large-file handling remain unimplemented.
- Plugin Manager is informational and scans local manifests only. Signed marketplace catalog verification, native DLL loading/lifecycle, plugin enable/disable, and online installation remain unimplemented; no third-party code is downloaded or executed.
- Xdebug support is an early CLI workflow: it requires Xdebug 3 already enabled in `php.exe`,
  listens on local port 9003, and supports a caret-line breakpoint plus step commands. Call
  stack, variable/watch inspection, conditional breakpoints, web requests, path mappings, and
  a graphical profiler/call graph remain unimplemented. Profile output reports first-event
  self-cost counts, not calibrated elapsed-time percentages.
- Code and Refactor menus explicitly identify unavailable PHP intelligence/refactoring. Run and Git menus currently provide only run-current-PHP-file and Git status, not full run configurations or Git workflows.
- `phpintel`, `ai-gateway`, `dbtools`, and other Rust services do not yet provide their planned end-user features. Local AI chat/model discovery, database connections/query UI, and Laravel/Symfony framework intelligence remain unimplemented.

## Next

1. Finish M2: document encoding/line-ending handling, tab/split management, project file watching, folding, and large-file behavior; verify the current editor and PowerShell terminal interactively.
2. Continue M3–M8 in order: parsing, semantic PHP index, diagnostics, full terminal/run support, Git, and signed plugin system with Laravel and Symfony support.
3. Implement M11 database tooling and M12 local-only AI after their service protocols, credentials, and end-to-end tests are ready.

## Decisions Log
See `docs/DECISIONS.md`
