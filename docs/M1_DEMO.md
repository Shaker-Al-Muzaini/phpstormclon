# M1 Demo: IDE Shell

From a Visual Studio 2022 x64 developer shell with Qt 6.8 installed:

```powershell
$qtRoot = "C:\Qt\6.8.3\msvc2022_64"
cmake --build build\windows-debug
$env:PATH = "$qtRoot\bin;$env:PATH"
& .\build\windows-debug\bin\ForgeIDE.exe
```

The shell currently provides:

- File, Edit, View, Navigate, Window, and Help menus, plus clearly marked future feature
  actions.
- A compact IDE-style composition: project/Git strip and project tree on the left, a wide
  central editor, one integrated terminal dock at the bottom, and caret/language/service
  indicators. Unimplemented Structure, AI Assistant, Problems, and Run placeholder panels are
  intentionally omitted to keep the workspace uncluttered.
- Original Forge vector icons for the toolbar, common actions, welcome buttons, project
  folders, PHP/Blade/JS/TS/JSON/CSS/HTML/YAML/Markdown/SQL/config files, and application
  window. Icon colors follow the active Graphite or Daylight theme; no third-party IDE
  artwork is used.
- Project file icons show language badges and hover tooltips identify the detected file
  language. Go to File (`Ctrl+Shift+N`) searches project files, and Recent Files (`Ctrl+E`)
  reopens recently used files. Quick file search skips common generated/vendor folders and
  is capped at 15,000 files to avoid unbounded scans.
- Project browser, basic text-file tabs, create/open/save/close, and unsaved-change prompts.
- Project and Terminal docks. The integrated terminal supports multiple independent PowerShell
  tabs in the same dock,
  uses compact custom close controls, renumbers remaining tabs after closing one, starts each
  shell in the open project, and runs commands and aliases such as `ls`, `dir`, and
  `Get-ChildItem`. Opening or switching projects changes each shell's working directory.
- Graphite and Daylight themes, a theme setting, and persisted window layout/geometry.
- The source editor currently has a line-number/folding gutter, click-to-fold PHP function bodies,
  active-line highlight, theme-aware font, Ctrl+mouse-wheel font zoom, language status, and basic
  lexical coloring for common PHP/Blade, web, Python, SQL, JSON, and YAML files. This is an early
  editor step, not tree-sitter parsing or semantic PHP intelligence.
- Tools → Plugin Manager lists local plugin manifests and the current language coverage. It
  does not execute native plugins or download marketplace packages; signed catalog verification
  and the plugin ABI loader are not implemented.
- A service status indicator; `phpintel` starts as a separate process when its executable is present.
- Current-file Find (`Ctrl+F`), Find Next (`F3`), Go to Line (`Ctrl+G`), and standard
  editing shortcuts are registered; end-to-end keyboard input still needs reliable live
  verification.
- In the focused terminal, Up/Down recalls command history, `Ctrl+L`, `cls`, `clear`, and
  `Clear-Host` clear the visible terminal,
  `Ctrl+Shift+C` copies a selection, and `Ctrl+Shift+V` pastes. This is a redirected PowerShell
  session rather than a full PTY; full-screen interactive programs, terminal emulation, and
  reliable process interruption are not provided.
- Run Current PHP File (`Shift+F10`) when PHP is installed, and Git Status from the Git menu.
- Xdebug CLI debugging and Cachegrind self-cost summaries are available when Xdebug 3 is
  installed and loaded for the PHP CLI interpreter. See [XDEBUG.md](./XDEBUG.md) for
  prerequisites and current limitations.
- The plugin manager is informational/local-only until
  signed marketplace distribution and native plugin loading are implemented.

Open a folder with **Open Project**, then double-click a text file in the Project tool window.
Theme and layout preferences are saved when the application closes.

This is an early shell milestone, not a complete PHP/Laravel/Symfony IDE. PHP parsing,
completion, diagnostics, debugging, Git, and the full custom M2 editor are not available yet.
The terminal is a PowerShell command session, not a full ConPTY terminal: full-screen
interactive programs, terminal emulation, profiles/tabs, and Ctrl+C process control remain
planned. Qt built-in dock widgets are currently used; Qt
Advanced Docking System integration remains part of unfinished M1 work.
