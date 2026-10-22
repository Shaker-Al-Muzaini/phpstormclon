# Forge IDE — Architecture Decisions

Format: `YYYY-MM-DD | Decision | Reason`

---

| Date | Decision | Reason |
|------|----------|--------|
| 2026-10-21 | Qt 6.8 LTS (6.8.x) | Latest Qt LTS with 5-year support window; Qt Widgets chosen over QML for IDE maturity and dock system compatibility |
| 2026-10-21 | Rust stable 1.99.1 (2026-10-13, latest stable at install time) | Latest stable at time of first build; Rust 2024 edition enabled |
| 2026-10-21 | CMake 4.4.4 | Latest stable with full Corrosion v0.6 compatibility |
| 2026-10-21 | Corrosion v0.6.1 | Latest stable CMake–Rust integration; pure CMake, no legacy tooling |
| 2026-10-21 | Qt Advanced Docking System v5.1.1 | Latest stable; LGPL-licensed; full Qt6 support with dark mode and CMake |
| 2026-10-21 | tree-sitter 0.25.x (Rust crate) | Latest stable track; used for incremental parsing |
| 2026-10-21 | tree-sitter-php v0.25.1 | Latest stable PHP grammar for tree-sitter |
| 2026-10-21 | vcpkg manifest mode | Reproducible, version-locked C++ dependencies per-project |
| 2026-10-22 | vcpkg builtin baseline `67972e92edd181d433927b42d8ab4bfa52ac164a` | Pin manifest dependency resolution to an official vcpkg repository revision; the bundled vcpkg binary version hash is not a registry commit |
| 2026-10-21 | nlohmann/json via vcpkg | Header-only, MIT license, used for settings/theme JSON in C++ |
| 2026-10-21 | spdlog via vcpkg | Fast, MIT-licensed C++ logging; integrates with Qt logging categories |
| 2026-10-21 | JetBrains Mono as default editor font | SIL OFL licensed; best-in-class coding legibility; official open alternative |
| 2026-10-21 | Inter as UI font | SIL OFL licensed; modern, highly legible at small sizes |
| 2026-10-21 | Lucide icon set | ISC licensed; consistent, modern SVG icons |
| 2026-10-21 | LSP 3.17 + forge/* methods over stdio | Standard protocol; allows phpintel to work with other editors; named pipes also supported |
| 2026-10-21 | Separate process per service | A crashing Rust service cannot crash the C++ UI; supervisor auto-restarts |
| 2026-10-21 | salsa-style incremental design for phpintel | Correct incremental computation without manual invalidation |
| 2026-10-21 | Apache-2.0 project license | Permissive; compatible with all allowed dependencies; no copyleft obligations |
| 2026-10-21 | Graphite (dark) as default theme | Preferred by most developers; follows accessibility contrast guidelines |
| 2026-10-21 | WiX Toolset v4 for Windows installer | Modern, maintained, free MSI generator; NSIS as fallback |
| 2026-10-21 | GitHub Actions (Windows runner) for CI | Free for open source; native Windows builds without cross-compilation |
| 2026-10-21 | MSVC (cl.exe) as primary compiler | Best Windows compatibility; clang-cl compatibility maintained |
| 2026-10-21 | tokio async runtime for Rust services | De-facto standard async runtime; good ecosystem support |
| 2026-10-21 | futures-util crate added to rpc-common deps | Required for StreamExt/SinkExt traits on FramedRead/FramedWrite |
| 2026-10-21 | bytes crate added to rpc-common | Required by tokio-util codec infrastructure |
| 2026-10-22 | futures-util `sink` feature enabled | Required for `SinkExt::send` when writing framed JSON-RPC responses |
| 2026-10-22 | Qt 6.8.3 MSVC 2022 x64 used for Windows verification | Latest available Qt 6.8 patch release in the official Qt archive; matches the project's Qt 6.8 LTS requirement |
