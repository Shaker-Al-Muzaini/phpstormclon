# Build and run on the existing Windows setup

The production configuration remains **Windows x64, Qt 6.8.x MSVC 2022, C++20, Rust 1.99.1, CMake + Ninja**. Existing `windows-debug` and `windows-release` presets and build directories are retained. No PHP configuration, global PATH, user project, or installed toolchain is rewritten.

From the existing checkout, fetch and switch to the feature branch using ordinary Git commands. Commit or stash your own local changes first; this workflow never uses reset/clean/force.

```powershell
cd D:\laravelProject\PhpStorm222
git fetch origin
git switch codex/native-workspace-redesign
powershell -File .\tools\build-local.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Run
```

Use your actual Qt directory. The script can also discover an existing Qt 6.8 installation under `C:\Qt`, or use `QT_ROOT_DIR` / `Qt6_DIR` in the current shell. Visual Studio Community, Professional, Enterprise and Build Tools are discovered using `vswhere`; the script does not assume a Build Tools-only path.

The script configures the existing preset, builds the C++ app and Rust services, runs CTest and Cargo tests, copies Qt runtime DLLs/plugins with the existing `windeployqt`, and optionally launches the application. The Rust `phpintel` executable is copied beside the app by CMake. It does not install or upgrade system tools. Errors stop the script before launch.

```powershell
# Release build and a portable development ZIP
powershell -File .\tools\build-local.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Configuration Release -Package
```

The build needs the existing Qt/MSVC/Rust/Ninja tools, Git, and vcpkg. `zlib` is the only added native package; vcpkg resolves it through the existing pinned baseline. CMake's default Rust integration remains enabled. First-time dependency resolution requires internet access; subsequent cached builds can be offline.

The GitHub Actions **ForgeIDE-windows-x64** artifact contains the app, Qt runtime, service executable and notices after a successful Windows build. This is a portable development build, not an MSI installer or a claim that the full language engine is complete.

## Working features in this change

- Compact toolbar/menu, project selector with recent folders, branch indicator, working tool stripes and project tree commands.
- Editor tabs, shared-document split right/down, close others/all, editable encoding/EOL/indent status and editor read-only switch.
- Ctrl+F find; F3 / Shift+F3 next/previous; Ctrl+R literal replacement with regex/case/word matching; Ctrl+Shift+F project search; Ctrl+Shift+N quick open; double Shift / Ctrl+Shift+A files and actions; Ctrl+J snippets.
- Six built-in themes: Graphite, Daylight, Midnight, Obsidian, Ocean and Amethyst. The app workspace uses neutral IDE colors. Only the startup splash uses Shaker's green/gold palette and SM monogram.
- Extensions: local VSIX/ZIP/folder installation, Open VSX search/download, enable/disable/uninstall, normalized VS Code themes, snippets and selected unconditional keybindings. Network is used only after a search/install action.
- Atomic saves with encoding/EOL retention, external-write conflict prompt, debounced file watching and atomic settings writes.

## Extension compatibility

Forge does not implement the VS Code JavaScript extension host or the JetBrains JVM plugin ABI. Those executable plugins cannot run just because they have been downloaded. Packages declaring `main`, `browser`, or `entryLibrary` are rejected with an explanation; unrelated declarative contributions are not presented as executable features.

Common VS Code UI colors and syntax scopes are imported. External theme includes, semantic-token rules, conditional/chord keybindings, language servers, icon packs, and advanced snippet transforms/variables are not implemented. Plain snippet bodies and numbered default placeholders are supported. Imported package license files are retained. Downloads/extraction have byte/file limits, CRC checks and path/symlink validation.

## Remaining milestones

Semantic PHP/Laravel intelligence, safe project-wide refactoring, ConPTY/TTY emulation, full Git/DBGp/database/local-AI features, native plugin loading and an installer still belong to the existing roadmap. The terminal remains a redirected PowerShell session, with explicitly configured UTF-8 I/O. No credentials or user PHP settings are changed.
