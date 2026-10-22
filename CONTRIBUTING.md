# Contributing to Forge IDE

Thank you for your interest in contributing to Forge IDE!

## Development Setup

### Prerequisites

- Windows 10/11 x64
- [Qt 6.8 LTS](https://www.qt.io/download-open-source) — select MSVC 2022 64-bit
- [Rust stable](https://rustup.rs/) — `rustup install stable`
- [CMake 4.x](https://cmake.org/download/)
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with C++ workload
- [Ninja](https://ninja-build.org/) (install via `winget install Ninja-build.Ninja`)
- [Git](https://git-scm.com/)

### Quick Start

```powershell
# 1. Clone the repo
git clone https://github.com/forge-ide/forge-ide.git
cd forge-ide

# 2. Run the bootstrap script (installs Rust, CMake, vcpkg if missing)
powershell -File tools/bootstrap.ps1

# 3. Open a Developer Command Prompt for VS 2022 (x64)
# Then configure:
cmake --preset windows-debug -DCMAKE_PREFIX_PATH="C:/Qt/6.8.x/msvc2022_64"

# 4. Build
cmake --build build/windows-debug

# 5. Run
build/windows-debug/bin/ForgeIDE.exe

# 6. Run Rust tests
cargo test --all
```

## Code Style

### C++
- C++20, no exceptions in hot paths, RAII everywhere
- `clang-format` with the project's `.clang-format` config (Google style base, 4-space indent)
- All public APIs documented with Doxygen comments
- No `#ifdef _WIN32` outside `platform/`

### Rust
- `cargo fmt` (enforced in CI)
- `cargo clippy -D warnings` (enforced in CI)
- No `unwrap()` across RPC boundaries — use `anyhow::Result` and propagate
- Structured logging with `tracing` only

## Pull Request Checklist

- [ ] `cargo fmt --all` and `cargo clippy --all-targets` pass
- [ ] `cmake --build` succeeds
- [ ] New code has tests
- [ ] Performance-critical paths have benchmarks
- [ ] `PROGRESS.md` updated if a milestone item is completed
- [ ] `docs/DECISIONS.md` updated if an architectural decision was made
- [ ] No proprietary material (JetBrains names, icons, code, themes)
- [ ] New dependencies pass license check (see `docs/DECISIONS.md` for allowed licenses)

## Issue Templates

Use GitHub Issues with the appropriate label:
- `bug` — something is broken
- `feature` — new capability request
- `milestone:M0` through `milestone:M13` — milestone-scoped work
- `performance` — budget violations

## License

All contributions are under Apache-2.0. By submitting a PR, you agree to
license your contribution under Apache-2.0.
