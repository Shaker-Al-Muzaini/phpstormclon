| Component | License | Source | Notes |
|-----------|---------|--------|-------|
| Qt 6.8 | LGPL v3 | qt.io | Dynamically linked. Qt DLLs shipped. Relinking supported. |
| Qt Advanced Docking System | LGPL v2.1 | github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | Planned; not bundled or linked by the current build. |
| JetBrains Mono font | SIL OFL 1.1 | github.com/JetBrains/JetBrainsMono | Preferred font if installed; font files are not bundled. |
| Inter font | SIL OFL 1.1 | rsms.me/inter | Preferred font if installed; font files are not bundled. |
| Lucide icons | ISC | github.com/lucide-icons/lucide | Planned; current icons and splash artwork are original project QPainter code. |
| Rust standard library | MIT / Apache-2.0 | rust-lang.org | Statically linked into service binaries. |
| tokio | MIT | crates.io/crates/tokio | Statically linked. |
| serde / serde_json | MIT / Apache-2.0 | crates.io/crates/serde | Statically linked. |
| tokio-util | MIT | crates.io/crates/tokio-util | Statically linked. |
| bytes | MIT | crates.io/crates/bytes | Statically linked. |
| futures-util | MIT / Apache-2.0 | crates.io/crates/futures | Statically linked. |
| tree-sitter | MIT | crates.io/crates/tree-sitter | Statically linked. |
| tracing | MIT | crates.io/crates/tracing | Statically linked. |
| tracing-subscriber | MIT | crates.io/crates/tracing-subscriber | Statically linked. |
| anyhow | MIT / Apache-2.0 | crates.io/crates/anyhow | Statically linked. |
| thiserror | MIT / Apache-2.0 | crates.io/crates/thiserror | Statically linked. |
| uuid | Apache-2.0 | crates.io/crates/uuid | Statically linked. |
| git2 | MIT | crates.io/crates/git2 | vcs service only. libgit2 linked. |
| libgit2 | GPL-2.0 with linking exception | libgit2.org | Dynamically linked via git2-sys. |
| sqlx | MIT / Apache-2.0 | crates.io/crates/sqlx | dbtools service only. |
| rustls | Apache-2.0 / ISC / MIT | https://github.com/rustls/rustls | Verified TLS for database connections. |
| webpki-roots 0.26.11 / 1.0.11 | CDLA-Permissive-2.0 | https://github.com/rustls/webpki-roots | Mozilla root certificate data; full licence reproduced below. |
| Intelephense 1.18.5 | Separate proprietary server licence | https://intelephense.com | Optional explicit user installation only; server is not bundled or redistributed. |
| zlib | Zlib | https://zlib.net/zlib_license.html | Used for bounded VSIX/ZIP decompression. |
| reqwest | MIT / Apache-2.0 | crates.io/crates/reqwest | ai-gateway service only. |
| nlohmann/json | MIT | github.com/nlohmann/json | C++ header-only. |
| spdlog | MIT | github.com/gabime/spdlog | C++ logging. |
| Atom One Dark Theme 2.3.0 | MIT | https://github.com/akamud/vscode-theme-onedark | Unmodified VSIX test fixture only; full license retained in the archive. |
| GoogleTest | BSD-3-Clause | github.com/google/googletest | Test binaries only, not shipped. |

This file is maintained manually. Run `cargo deny check licenses` for Rust license verification.
User-installed declarative extension packages retain their supplied license files. The current CI does not regenerate this document.

## webpki-roots certificate data licence

# Community Data License Agreement - Permissive - Version 2.0

This is the Community Data License Agreement - Permissive, Version
2.0 (the "agreement"). Data Provider(s) and Data Recipient(s) agree
as follows:

## 1. Provision of the Data

1.1. A Data Recipient may use, modify, and share the Data made
available by Data Provider(s) under this agreement if that Data
Recipient follows the terms of this agreement.

1.2. This agreement does not impose any restriction on a Data
Recipient's use, modification, or sharing of any portions of the
Data that are in the public domain or that may be used, modified,
or shared under any other legal exception or limitation.

## 2. Conditions for Sharing Data

2.1. A Data Recipient may share Data, with or without modifications, so
long as the Data Recipient makes available the text of this agreement
with the shared Data.

## 3. No Restrictions on Results

3.1. This agreement does not impose any restriction or obligations
with respect to the use, modification, or sharing of Results.

## 4. No Warranty; Limitation of Liability

4.1. All Data Recipients receive the Data subject to the following
terms:

THE DATA IS PROVIDED ON AN "AS IS" BASIS, WITHOUT REPRESENTATIONS,
WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED
INCLUDING, WITHOUT LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE,
NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

NO DATA PROVIDER SHALL HAVE ANY LIABILITY FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING
WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE DATA OR RESULTS,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

## 5. Definitions

5.1. "Data" means the material received by a Data Recipient under
this agreement.

5.2. "Data Provider" means any person who is the source of Data
provided under this agreement and in reliance on a Data Recipient's
agreement to its terms.

5.3. "Data Recipient" means any person who receives Data directly
or indirectly from a Data Provider and agrees to the terms of this
agreement.

5.4. "Results" means any outcome obtained by computational analysis
of Data, including for example machine learning models and models'
insights.
