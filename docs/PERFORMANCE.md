# Forge IDE — Performance Budgets

Performance targets per the master spec (Section 5). These are enforced by benchmarks
stored as CI artifacts. A build fails on > 20% regression vs. the baseline.

## Targets

| Metric | Budget | Implementation Note |
|--------|--------|---------------------|
| Cold start to interactive window | < 1.5 s | Measured: time from process launch to first paint of main window |
| Typing latency (5 MB file) | < 16 ms/keystroke | Measured: key event → document update → repaint |
| Open 100k-line file | < 300 ms | Measured: file read + tokenize + first paint |
| Scrolling | 60 fps smooth | Measured: frame time during continuous scroll |
| First index (20k PHP files incl. vendor) | < 60 s | Measured: cold phpintel index on a fresh Laravel app |
| Incremental index after save | < 200 ms | Measured: file change → phpintel update → diagnostics delivered |
| Idle memory (UI + services, typical project) | < 600 MB | Measured: RSS of all processes after 5 min idle |

## Measurement Methodology

- **Startup**: `Get-Date` before launch, Qt `QElapsedTimer` at first `MainWindow::show()` call
- **Typing latency**: `QElapsedTimer` from `QKeyEvent` handler start → `update()` call end
- **File open**: `QElapsedTimer` from `open()` invocation → `paintEvent()` first call
- **Index time**: `Instant::now()` in phpintel at index start and `$ready` notification time
- **Memory**: Task Manager RSS sum of ForgeIDE.exe + all service PIDs

## CI Enforcement

Benchmarks are run as a separate CI job (`benchmark` in ci.yml), storing results to
`benchmark-results/` artifacts. The job fails with a clear error if any metric regresses
by more than 20% from the stored baseline.

Baseline is stored in `tests/benchmarks/baseline.json`. Update it by running:
```powershell
cargo bench --all -- --save-baseline main
cmake --build build/windows-release
.\tools\update-baseline.ps1
```

## Known Bottlenecks (M0)

- Qt startup time: loading Qt DLLs on cold disk (~200 ms). Mitigated by DLL preloading.
- phpintel startup: Rust binary startup + stdin/stdout setup (~100 ms). Acceptable.
- First index: vendor/ has ~15k files. Parallelized with rayon in phpintel.

## Notes for Future Milestones

- **M2**: Editor must use a rope data structure for O(log n) inserts on large files
- **M3**: tree-sitter incremental parse — only re-parse changed regions
- **M4**: Index must use salsa's memoization to avoid re-running expensive queries
- **M6**: Terminal: PTY I/O on a separate thread, never blocking the Qt event loop
