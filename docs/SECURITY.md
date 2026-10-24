# S1 Dependency Security Notes

## Audit baseline

The S1 dependency audit was run with `cargo-deny 0.20.2` and `cargo-audit`
against the RustSec advisory database. The repository contains an explicit,
temporary exception for `RUSTSEC-2023-0071` (RSA Marvin timing attack) in
`deny.toml`.

The advisory has no patched `rsa` release. At this baseline, `rsa 0.9.10` is
transitive through `sqlx-mysql`. The database connection implementation now
reassesses this exception against SQLx 0.8.6's `connection/auth.rs`: the client
imports `RsaPublicKey` and calls public-key `encrypt` (OAEP), without RSA
private-key decryption or signing. The advisory concerns private-key timing.
This limits the exposure of this call path; it does not fix the upstream crate.
Remote MySQL and PostgreSQL URLs require verified TLS (`ssl-mode=verify_identity`
and `sslmode=verify-full`, respectively). Explicit TLS disable is accepted only
for loopback hosts. Credentials remain in memory and stdio, not arguments or
settings; connection errors are redacted. SQLite is local and requires no TLS.
Remove the exception when a safe upstream resolution is available. See
https://rustsec.org/advisories/RUSTSEC-2023-0071.html and SQLx 0.8.6 source.

The query editor defaults to a read-only database session and rejects write/session
control statements in that mode. This is an accident-prevention measure, not a
sandbox for untrusted SQL functions. Users explicitly reconnect with read-only
disabled to write. Queries have row, result-size and time limits. Cancelling or
timing out a write closes the connection but cannot guarantee a server did not
commit it; the UI tells the user to inspect the outcome before retrying.

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
