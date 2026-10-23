# Forge IDE — Performance targets

These are design targets, not measured results or current CI guarantees:

| Metric | Target |
| --- | --- |
| Cold start | < 1.5 s |
| Typing latency at 5 MiB | < 16 ms |
| Open a 100k-line file | < 300 ms |
| First PHP index | < 60 s for 20k files |
| Incremental semantic index | < 200 ms |
| Idle total memory | < 600 MiB |

There is currently no benchmark job, committed baseline, or automatic regression threshold. The PHP semantic index is not implemented. Background file-list/text search and basic large-file guards reduce UI work, but do not establish these budgets. Opening files still reads the complete file synchronously; files above 64 MiB are rejected, and highlighting/folding are disabled above 200k characters. Repeatable benchmarks remain roadmap work.
