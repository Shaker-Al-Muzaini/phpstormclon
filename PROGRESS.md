# Forge IDE — Progress

## Status

**Current session: Diagnostics and Problems — Windows app/tests build succeeds; Rust and C++ tests are being verified**
**Product milestone: M1 — Shell and Docking (in progress)**

---

## 2026-10-24 diagnostics and Problems

- Added a Qt diagnostics model with per-file/source replacement, change signals, PHP lint output parsing, and bounded Laravel exception/stack-frame parsing.
- Added asynchronous `php -l` checks on save with a five-second timeout, optional setting, PHP-not-found status, and phpintel precedence when syntax diagnostics are available.
- Added the Problems dock with file/runtime grouping, severity icons, error/warning/current-file filters, source-line context, and navigation to source locations.
- Added editor diagnostic underlines and tooltips, scrollbar markers, next/previous inspection controls, status-bar counts, and theme-token colors.
- Added incremental tree-sitter-php syntax diagnostics through LSP notifications; a missing semicolon is reported at the previous token's end, and fixed documents clear diagnostics.
- Laravel log updates are watched and debounced; parsing reads at most the latest 2 MiB.
- Verification: Windows Qt/MSVC Debug build of `ForgeIDE.exe` and `forge_tests.exe` succeeds; Rust workspace tests pass. CTest now discovers and runs the suite using the Qt offscreen plugin path; 30/31 C++ tests pass, including all diagnostics and Problems tests and the application startup screenshot. The remaining failure is the existing `ServiceSupervisor.DisablesServiceAfterRestartLimit` test, outside this diagnostics change.
- Not implemented/covered: PHP semantic diagnostics, quick fixes, and a timed end-to-end GUI assertion for the one-second acceptance threshold.

## 2026-10-23 workspace continuation

- Continued the existing Qt/C++ + Rust codebase on `codex/native-workspace-redesign`; no re-scaffolding or production toolchain changes.
- Replaced classic chrome with a compact toolbar, hamburger menu, recent-project selector, branch indicator, functioning stripes, project header/context menu, and updated status controls.
- Added a separate original startup splash: Shaker Al-Muzayni / شاكر المزيني, SM monogram, portfolio forest/mint/gold. The workspace keeps neutral IDE colors, as clarified by the user.
- Added Graphite, Daylight, Midnight, Obsidian, Ocean, Amethyst; live switching, persistent selection and theme-aware UI/editor/terminal colors.
- Added inline find/replace (case/word/regex, literal replacement, undoable replace-all), project search on background workers, file/action quick open, double-Shift and conventional shortcuts.
- Added shared-document split right/down, tab context actions, recent files, editable encoding/EOL/indent controls and read-only mode.
- Activated declarative extensions: Open VSX search/download; VSIX/ZIP/folder imports; enable/disable/uninstall; theme/snippet/keymap contributions. Tested a real MIT One Dark 2.3.0 VSIX from Open VSX. Executable/JVM/Node extensions remain unsupported and are explicitly rejected.
- Added file watching and external-overwrite confirmation, atomic settings writes, UTF-8 terminal I/O configuration, and a shutdown lifecycle fix found with AddressSanitizer.
- Split the former 3,000-line MainWindow file into focused implementation files and reusable widgets. Fixed resource embedding (AUTORCC), which previously prevented the app from loading bundled themes/keymaps.
- Preserved Windows presets and added `tools/build-local.ps1` with vswhere-based MSVC discovery, CTest/Cargo checks, Qt deployment and portable packaging.
- Hosted verification fixed the previous MSVC setup failure. Rust checks passed; Windows C++ formatting passed after enforcing source LF endings. The next run exposed missing pinned vcpkg registry history in the runner checkout; the build now fetches that official commit when absent without changing the installed checkout. The full Windows Qt/MSVC/Cargo build then succeeded, with 26/27 CTest cases passing; the remaining fixed-pitch font check exposed the headless plugin's separate font database. Explicit installed mono fallbacks and a Windows font directory for offscreen tests are now included; final rerun is pending.
- Fixed editor font inheritance so UI styling cannot replace its monospace font; added a fixed-pitch UI regression assertion.
- Local verification: the C++ workspace builds on Linux Qt 6.4.2 using the explicit UI-only compatibility switches; 27/27 tests passed, including real deflated VSIX import and the startup screenshot test; the suite also passed in an AddressSanitizer build (leak detection disabled for external Qt libraries). Final results are recorded in `docs/WORKSPACE_REDESIGN.md`. Production defaults remain Qt 6.8 / Rust 1.99.1 / MSVC on Windows; new hosted CI is tracked separately.
- Remaining gaps: Qt built-in docking rather than ADS/auto-hide, one additional editor split rather than arbitrary pane trees, limited declarative extension compatibility, redirected terminal rather than ConPTY, and the existing PHP/Laravel/Git/debugger/database/AI roadmap. No semantic analysis or benchmark budgets are claimed complete.

## Earlier milestone history

## Done

### M0 — Foundations (complete)
- [x] Repository layout created (all directories from spec section 4)
- [x] `CMakeLists.txt` — top-level with Corrosion + Qt6 + vcpkg
- [x] `CMakePresets.json` — windows-debug and windows-release presets
- [x] `Cargo.toml` — workspace with all 6 service crates
- [x] `rust-toolchain.toml` — pinned to Rust 1.99.1
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

### S1 — Data integrity and service lifecycle (in progress)
- [x] Preserve UTF-8/BOM, UTF-16 LE/BE, Windows-1256/1252, mixed line endings, and reject invalid encodings instead of silently replacing text.
- [x] Save files atomically; retain encoding and per-line endings when saving.
- [x] Bound and validate JSON-RPC frames; fail outstanding requests on malformed protocol data or service exit.
- [x] Make service shutdown asynchronous and bounded, capture stderr logs, and reset restart backoff after stable uptime.
- [x] Pin Rust 1.99.1, require GoogleTest for C++ test configurations, and tighten CI/dependency checks.
- [x] Add RPC/service lifecycle, encoding, theme resource, and keymap tests.
- [x] Windows Debug build and all 13 C++ tests pass; Rust workspace tests and Clippy pass.
- [x] Run `cargo audit` against the cached RustSec database (281 dependencies); no other advisories were reported. The local registry yanked-crate lookup timed out, so hosted CI must verify the complete audit.
- [x] Record the temporary RSA Marvin advisory exception and dependency audit limitations in `docs/SECURITY.md`.
- [ ] Verify hosted CI after pushing the S1 changes before marking S1 complete.

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
- The terminal has multiple redirected PowerShell sessions and UTF-8 configuration; ConPTY, full ANSI/TTY behavior, profiles and robust Ctrl+C remain planned.
- The current editor has a gutter and basic lexical highlighting, but not tree-sitter parsing or semantic tokens. Encoding detection and line-ending preservation now cover UTF-8, UTF-16, and Windows code pages; tabs and one shared-document split now work; large-file limits/highlighting fallback are present, but streaming and the rope buffer remain unimplemented.
- Plugin Manager now installs supported declarative packages through Open VSX/local archives. Native DLL/JVM/JavaScript extension hosts, semantic language plugins, signed native catalog verification and icon packs remain unimplemented. Downloaded executable code is never loaded.
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
