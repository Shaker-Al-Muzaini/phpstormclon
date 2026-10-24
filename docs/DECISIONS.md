# Forge IDE — Architecture Decisions

Format: `YYYY-MM-DD | Decision | Reason`

---

| Date | Decision | Reason |
|------|----------|--------|
| 2026-10-24 | Use a local LSP PHP provider for semantic completion and indexing; retain Rust syntax fallback | The current parser only supplies syntax recovery regions. A standard client can provide real symbol/type intelligence without pretending regex matches are semantic analysis. Intelephense's free features cover the requested operations; its server is installed explicitly, not redistributed. |
| 2026-10-24 | Keep terminal suggestions local and execution explicit | History ghost text and completion must never submit a suggested command. Accept with Right/Tab, then execute separately with Enter. |
| 2026-10-24 | Add bounded developer tools with in-memory database credentials | User requested Git diff/commits, SQL tools and PHP analyzers. Use argument-based subprocesses/RPC, asynchronous UI, database read-only transactions, row/time limits and no automatic project dependency changes. |
| 2026-10-24 | Use tree-sitter-php for parser syntax diagnostics; retain optional async `php -l` on save | Provide responsive incremental syntax feedback without blocking the Qt UI, while preserving a native PHP interpreter check |
| 2026-10-24 | Bound Laravel log parsing to the latest 2 MiB and debounce file-watch updates | Avoid unbounded memory/CPU use when runtime logs grow quickly |
| 2026-10-21 | Qt 6.8 LTS (6.8.x) | Latest Qt LTS with 5-year support window; Qt Widgets chosen over QML for IDE maturity and dock system compatibility |
| 2026-10-21 | Rust stable 1.99.1 (2026-10-13, latest stable at install time) | Latest stable at time of first build; Rust 2024 edition enabled |
| 2026-10-21 | CMake 4.4.4 | Latest stable with full Corrosion v0.6 compatibility |
| 2026-10-21 | Corrosion v0.6.1 | Latest stable CMake–Rust integration; pure CMake, no legacy tooling |
| 2026-10-21 | Qt Advanced Docking System v5.1.1 | Latest stable; LGPL-licensed; full Qt6 support with dark mode and CMake |
| 2026-10-21 | tree-sitter 0.25.x (Rust crate) | Latest stable track; used for incremental parsing |
| 2026-10-21 | tree-sitter-php v0.25.1 | Latest stable PHP grammar for tree-sitter |
| 2026-10-21 | vcpkg manifest mode | Reproducible, version-locked C++ dependencies per-project |
| 2026-10-22 | vcpkg builtin baseline `67972e92edd181d433927b42d8ab4bfa52ac164a` | Pin manifest dependency resolution to an official vcpkg repository revision; the bundled vcpkg binary version hash is not a registry commit |
| 2026-10-23 | Fetch missing pinned vcpkg history before configuration | Hosted Windows checkout lacked the baseline object. Verify `versions/baseline.json`, fetch the exact commit from the official registry only when absent; do not reset or replace the user's installed checkout. |
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
| 2026-10-23 | Pin CI and local Rust builds to 1.99.1 | Reproducible service builds and identical CI/local toolchain behavior |
| 2026-10-23 | Preserve source encoding and each existing line ending; use QSaveFile for writes | Avoid corrupting legacy Arabic/Windows-encoded files or truncating originals on interrupted saves |
| 2026-10-23 | Cap JSON-RPC frames at 64 MiB and headers at 8 KiB; fail pending calls when the peer exits | Bound untrusted IPC input and make service failures observable to request handlers |
| 2026-10-23 | Keep the RSA Marvin advisory visible but narrowly acknowledged in dependency policy | No fixed rsa version is available; it is currently only pulled in transitively by the dbtools SQLx MySQL feature, whose database functionality is not yet implemented |
| 2026-10-23 | Upgrade git2 and Tokio to patched releases; remove unused portable-pty dependency | Resolve reported unsoundness/race advisories and avoid an unmaintained serial dependency pulled by an unused terminal skeleton |

| 2026-10-23 | Continue existing Qt/Rust sources on a feature branch; do not scaffold a replacement | User explicitly requested continuation and preserving their Windows environment |
| 2026-10-23 | Neutral IDE colors for workspace; portfolio green/gold only on startup splash | User clarified the visual reference and branding boundary |
| 2026-10-23 | Declarative VSIX/ZIP imports and Open VSX registry; reject executable plugin hosts | Provide working interoperable themes/snippets/keymaps without falsely claiming JVM/Node extension compatibility |
| 2026-10-23 | Keep production Qt 6.8/Rust pins; add explicit UI-only build options for local validation | Linux review SDK differs from the user's Windows setup; defaults remain unchanged |
| 2026-10-23 | Use Visual Studio Developer PowerShell via vswhere in CI and local helper | Avoid the failing third-party MSVC setup action and hard-coded Build Tools paths |

- 2026-10-23: Windows offscreen tests explicitly use the installed Windows font directory. Qt 6.8.3's offscreen plugin uses FreeType; the native system FixedFont alias alone is insufficient there. Prefer installed monospace families for editor fallback and retain the fixed-pitch regression check.

| 2026-10-24 | Use an explicitly installed local Intelephense 1.18.5 process for free PHP completion, definitions, symbols, diagnostics and formatting | A mature semantic provider improves PHP support now while the Rust parser remains a syntax fallback; the proprietary server is not redistributed, no global npm/project Composer changes occur, and no premium features are claimed |
| 2026-10-24 | Keep terminal suggestions in the existing redirected shell backend | Gray history completion and async path suggestions are useful independently of a full terminal emulator; bounded history stays in memory, default prompts track cwd, and ConPTY remains a documented gap |
| 2026-10-24 | Implement developer tooling through actual local processes and SQLx, with bounded results | Git operations use explicit selection and commit-message stdin; quality tools use project configuration; database credentials stay in memory and remote TLS is verified. SQLite has live coverage; MySQL/PostgreSQL remain unverified against live servers |
