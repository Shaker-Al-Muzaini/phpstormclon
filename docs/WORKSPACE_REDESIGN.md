# Workspace redesign verification

This change continues commit `494bffc` and retains the native Qt Widgets + Rust architecture. The reference screenshots guide spacing, toolbar/stripe placement and neutral IDE colors; no proprietary images or icons are copied. Startup artwork is original QPainter rendering, using the portfolio palette verified from `MyIdentity/src/index.css`: forest `#07291f`, deep `#051d16`, mint `#6edca0`, gold `#d4a017`.

## Scope and visual differences

The toolbar, hamburger menu, project selector, tool stripes, project tree, editor tabs and terminal are implemented using Qt Widgets. The docking backend is still Qt's built-in system, not Qt Advanced Docking System. Dock resize/float and persisted layouts work; full pin/auto-hide stripes and arbitrary multi-pane layouts are not implemented. The editor supports one additional shared-document split, horizontally or vertically.

Unsupported panels and semantic-language actions remain absent rather than showing fabricated diagnostics, AI messages, or branch/PR data. Icons are original QPainter glyphs. The workspace uses Graphite/Daylight and the other bundled themes; personal branding is confined to the startup splash. UI fonts prefer the existing theme fonts and fall back to locally installed fonts; no new font binaries are bundled.

Screenshots under `docs/screens/` are captures of the native app on Linux Qt 6.4.2 with the optional UI-only build, not Windows screenshots. They are used to inspect the layout and color changes. Native Windows 6.8/MSVC verification is tracked separately by the PR's CI; PowerShell and Windows code-page tests need that environment. The sample fixture resembles a Laravel layout solely for UI/search tests and has no framework dependencies.

## Verification commands

The local review environment uses `FORGE_BUILD_RUST=OFF` and `FORGE_QT_MIN_VERSION=6.4` to run the C++ GUI and tests with the available Qt SDK. These flags do not change the default Windows configuration. Rust source and Cargo dependencies are unchanged.

```sh
cmake -S . -B build/linux-review -G Ninja -DFORGE_BUILD_RUST=OFF -DFORGE_QT_MIN_VERSION=6.4
cmake --build build/linux-review --parallel 4
ctest --test-dir build/linux-review --output-on-failure
```

An AddressSanitizer build found a shutdown callback accessing the terminal view after destruction. Child-process/UI callbacks are now disconnected and processes stopped while MainWindow members remain alive; the regression is covered by the workspace and startup tests.

The tests exercise actual keyboard find/save dispatch, replace-all undo, an Arabic filename with CRLF, protection against external overwrites, split-document edits, extension activation/rejection, ZIP validation, background project search and file-watch reattachment. Screenshots are visual evidence only; no startup/index/typing benchmark budget is claimed to have been achieved.

Local result: **27/27 CTest cases passed**, including the real Open VSX One Dark 2.3.0 fixture. The same suite passed with AddressSanitizer enabled (`ASAN_OPTIONS=detect_leaks=0`). Clang-format 19.1.5 and `git diff --check` passed. Graphite/Daylight 100% and Obsidian/RTL 150% native screenshots were captured successfully. Marketplace metadata and the real package download were checked against Open VSX; interactive Windows networking/PowerShell validation remains subject to Windows CI/manual use.

## Hosted Windows verification

The Windows Qt 6.8.3 / MSVC / Rust build succeeded in run [40017935741](https://github.com/Shaker-Al-Muzaini/phpstormclon/actions/runs/40017935741). Rust and dependency-policy/audit jobs passed with the existing documented RSA advisory exception. CTest passed 26/27 cases; the sole failure was fixed-pitch font selection in the offscreen platform.

Qt's Windows offscreen backend uses its FreeType database and `QT_QPA_FONTDIR`, rather than the native Windows font database (Qt 6.8.3 sources: `qoffscreenintegration.cpp`, `qfreetypefontdatabase.cpp`, `qplatformfontdatabase.cpp`). Tests now supply the installed Windows Fonts directory, and the editor chooses an installed mono fallback when the requested family is missing. The font regression assertion remains enabled. The corrected run is pending; a Windows portable artifact is published only after all CTest cases pass.
