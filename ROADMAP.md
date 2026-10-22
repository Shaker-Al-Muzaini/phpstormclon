# Forge IDE — Roadmap

This file mirrors the milestone plan from the master specification.

## Legend
- ✅ Complete (builds, tests pass, acceptance criteria met)
- 🔄 In Progress
- ⏳ Planned
- ❌ Blocked

---

## M0 — Foundations 🔄
- Repository layout (all directories from spec §4)
- CMake + Cargo + Corrosion + vcpkg build system
- GitHub Actions CI (Windows runner)
- License, contributing, code of conduct, security docs
- Empty Qt main window
- `ServiceSupervisor` — starts/restarts Rust services with exponential back-off
- `RpcClient` — JSON-RPC 2.0 over stdio, LSP framing
- `ActionManager` — central action registry, keymap data
- `Settings` — JSON-backed settings with per-project overrides
- Platform abstraction layer
- `rpc-common` Rust crate — shared types and framing codec
- `phpintel` skeleton — responds to `$/ping` and `$/shutdown`; emits `$/ready`
- All other service skeletons (vcs, dbgp, dbtools, term, ai-gateway)
- `docs/ARCHITECTURE.md`, `docs/DECISIONS.md`, `docs/PERFORMANCE.md`
- Theme files (Graphite, Daylight)
- Keymap files (default PhpStorm-compatible, VS Code)
- Plugin SDK C ABI header
- First-party plugin manifests (laravel, blade)

## M1 — Shell and Docking ⏳
- Qt Advanced Docking System (v5.1.1) integration
- Full main menu (File, Edit, View, Navigate, Code, Refactor, Run, Tools, Git, Window, Help)
- Toolbar: project selector, VCS branch widget, run config selector, Search Everywhere button
- Tool window stripes (left, right, bottom) with icon buttons
- Status bar: caret position, file info, PHP version, Git branch, service status, memory
- Theme engine: load Graphite/Daylight from JSON; full palette applied to Qt stylesheet
- Keymap loading from JSON — `ActionManager.applyKeymap()`
- Settings dialog skeleton (tree navigation, search bar)
- Layout save/restore per project (dock positions, tool window visibility)
- i18n infrastructure (Qt `.ts` files), English complete, RTL layout support
- High-DPI correctness verified at 100%, 125%, 150%, 200%

## M2 — Editor Core ⏳
- Custom editor widget on `QAbstractScrollArea`
- Rope-based document buffer (O(log n) insert/delete at any position)
- Custom gutter: line numbers, folding icons, breakpoint margin, VCS change markers
- Multi-caret editing (add/remove carets, column selection)
- Undo/redo stack (coalescing, grouping, labeled steps)
- Soft wrap, large-file mode (> 10 MB: syntax off, streaming)
- Find/Replace with regex, whole word, case options
- Tabs and splits (vertical and horizontal, drag to rearrange)
- File open/save with encoding detection and line-ending selection
- Project tree with `QFileSystemModel` + `.gitignore` filtering
- File watcher (`QFileSystemWatcher`) with debouncing
- Quick Open (`Ctrl+Shift+N`) — fuzzy file search
- Benchmarks: typing latency < 16 ms, 100k-line open < 300 ms, 60 fps scroll

## M3 — Parsing and Highlighting ⏳
- `phpintel`: tree-sitter for PHP, Blade, HTML, CSS, JS/TS, JSON, YAML, Markdown
- Incremental parse on edit (tree-sitter `edit()` + re-parse changed regions only)
- Syntax coloring via semantic tokens from `phpintel`
- Folding regions from tree-sitter CST
- Bracket matching (pairs, rainbow brackets)
- Breadcrumbs bar (namespace → class → method)
- Benchmark: incremental re-parse < 5 ms for typical single-line edit

## M4 — Semantic Index and Navigation ⏳
- `phpintel`: project-wide symbol index (namespaces, classes, interfaces, traits, enums, etc.)
- Composer/PSR-4 autoload resolution; vendor/ indexing
- PHP stubs for built-ins (bundled, permissive license)
- Go to Declaration (`Ctrl+B`), Go to Implementation (`Ctrl+Alt+B`)
- Find Usages (`Alt+F7`) with tool window
- Search Everywhere (double Shift): All / Classes / Files / Symbols / Actions tabs
- Go to Class (`Ctrl+N`), Go to File (`Ctrl+Shift+N`), Go to Symbol (`Ctrl+Alt+Shift+N`)
- Recent Files (`Ctrl+E`), Recent Locations (`Ctrl+Shift+E`)
- Structure tool window (document outline)
- Find in Files with scope selector and preview
- Hover / Quick Documentation (`Ctrl+Q`)
- Performance: Search Everywhere opens < 50 ms; indexing 20k files < 60 s

## M5 — Completion, Diagnostics, Formatting ⏳
- Type inference (docblocks, native types, union/intersection, static/self, array shapes, PHPStan annotations)
- Completion: members, namespaces, imports, named arguments, array keys, string literals
- Signature help (`Ctrl+P`) and inlay hints (parameter names, types)
- Diagnostics: syntax errors instantly, then semantic (undefined, wrong arity, type mismatch, unused, deprecated)
- Problems tool window
- Quick fixes (`Alt+Enter`): import class, implement methods, add use, create method, generate
- Formatter: PSR-12 + configurable style; Organize imports
- External tool integration: PHPStan/Larastan, Psalm, Pint, PHP-CS-Fixer

## M6 — Terminal, Runs, Tests, Composer ⏳
- Terminal tool window: multiple tabs, ConPTY, PowerShell/cmd/WSL/Git Bash profiles
- Run configurations: PHP script, artisan serve, built-in server, Composer script, npm script
- Interpreter management (local, WSL, Docker)
- PHPUnit and Pest test discovery + gutter run icons + test tree + rerun failed
- Coverage via Xdebug/PCOV
- Composer UI: require/remove/update, `composer.json` schema, outdated packages
- Artisan command palette (`php artisan list --format=json`)

## M7 — Git ⏳
- Full git integration via `vcs` service (git2)
- Status panel: staged/unstaged changes, stage hunks and lines
- Commit with message templates, amend
- Branch/tag/stash, checkout, merge, rebase
- Three-way merge tool with conflict resolution UI
- Cherry-pick, blame in gutter, file history
- Log graph (full branch visualization)
- Diff viewer: side-by-side and unified
- Remote operations: fetch/pull/push with credential helper
- Shelf (stash), pull requests (GitHub/GitLab links)

## M8 — Laravel Plugin + v0.1.0 Release ⏳
- Plugin system complete (manifest loading, DLL loading, extension point registry)
- `laravel` plugin: routes, Blade, Eloquent, config/env, facades, artisan wizards
- Migrations → schema → Eloquent completion chain
- Testing integration: PHPUnit/Pest via plugin
- Composer plugin UI
- Docker plugin
- Windows MSI installer (WiX v4) + portable ZIP
- Signed build pipeline
- Tag **v0.1.0**

## M9 — Refactoring Suite ⏳
- Rename: symbols, files, namespaces (preview, conflict detection, undo)
- Extract: method, variable, constant, parameter
- Inline, Move class (update namespaces + imports)
- Change signature, Safe delete
- Pull members up / Push down
- Convert to constructor promotion
- All refactorings: preview dialog + single-step undo

## M10 — Debugger ⏳
- Xdebug 3 DBGp listener via `dbgp` service
- Breakpoints: line, conditional, logpoint, exception, function
- Step controls: over, into, out, run to cursor
- Call stack, variables (lazy expansion), watches, evaluate expression
- Path mapping (Docker, WSL, remote)
- Zero-configuration validation page + setup assistant
- Debug web requests, CLI scripts, and tests

## M11 — Database Tools ⏳
- Database connections: MySQL/MariaDB, PostgreSQL, SQLite
- Schema browser (tables, columns, indexes, foreign keys)
- Table data grid with inline editing
- Query console: SQL completion, execution plans, history
- Export/import: CSV, JSON, SQL dump
- ER diagram view (basic)
- Saved queries, SSH tunnel support
- Credentials via Windows Credential Manager

## M12 — Local AI Assistant ⏳
- Providers: Ollama (HTTP localhost) and llama.cpp (OpenAI-compatible endpoint)
- Chat with project context (retrieval over symbol index + file chunks)
- Inline completion (fill-in-the-middle with small coder model)
- Explain error, explain selection, generate tests, generate PHPDoc
- Refactor suggestions shown as diffs (accept/reject)
- First-run assistant (detect installed Ollama models)
- Local-only badge in all AI UI; no data leaves the machine

## M13 — Polish and Parity ⏳
- Live templates and snippets (editable, with variables)
- Local file history
- EditorConfig support
- Docker/Compose run configurations
- Vue, React, Tailwind depth improvements
- Remote interpreter/SSH support
- PHP profiling (Xdebug profiler output viewer)
- Performance tuning to hit all budgets in §5
- Accessibility pass (full keyboard operability, screen reader basics)
- Arabic translation (RTL layout verification)
- Crash-safe recovery (session restore after crash)
- Documentation website
