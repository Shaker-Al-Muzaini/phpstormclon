# S1 Dependency Security Notes

## Audit baseline

The S1 dependency audit was run with `cargo-deny 0.20.2` and `cargo-audit`
against the RustSec advisory database. The repository contains an explicit,
temporary exception for `RUSTSEC-2023-0071` (RSA Marvin timing attack) in
`deny.toml`.

The advisory has no patched `rsa` release. At this baseline, `rsa 0.9.10` is
transitive through `sqlx-mysql`, included by the `dbtools` service skeleton. The
service does not yet implement database connections or RSA private-key
operations. Do not treat this as a general exemption: reassess the dependency
before implementing MySQL connections, and remove the exception as soon as a
safe upstream resolution is available.

The workspace updated `git2` to 0.21.0 to resolve three reported unsoundness
advisories and Tokio to 1.54.1 to resolve a timer race. The unused
`portable-pty` dependency was removed from the terminal skeleton, eliminating
its unmaintained `serial` transitive dependency.

`cargo deny check` passes with warnings for multiple transitive versions
(including `base64`, `thiserror`, and Windows support crates). Those warnings
are retained rather than hidden; consolidate versions when compatible
dependency updates are practical.

Locally, `cargo audit --no-fetch --no-yanked --ignore RUSTSEC-2023-0071` scanned
all 281 locked dependencies against the cached advisory database and completed
without reporting other advisories. The normal audit command could not complete
its registry yanked-crate lookup because the registry request timed out. Thus,
the local result does not verify yanked status; the hosted CI audit still runs
the full command and must be checked before calling the S1 security gate
complete.

## Required checks

CI runs `cargo deny check` and `cargo audit --ignore RUSTSEC-2023-0071`. The
single ignored advisory is tracked above and in `deny.toml`; all other
advisories remain blocking. The complete audit and hosted CI result must be
reviewed before calling the S1 security gate complete.
