# Security review — app-shell-demo-import

**Date:** 2026-10-01 · **Scope:** engine, importer, Tauri shell, UI, CI, dev tooling.

## Trust boundaries

| Input | Trust | Where it is parsed | Controls |
| --- | --- | --- | --- |
| `.dem` (other players' data, server-controlled content) | untrusted | `gmdr-import` child only | inherited handles only (no paths), Job Object: 4 GB, one process, kill-on-close, die on unhandled exception, UI restrictions; mitigation policies (no dynamic code, no remote/low-IL images, no extension points, strict handles after validation); 60 s idle watchdog; limits before every allocation; fuzzed |
| Map BSP pakfile (map can come from a server download) | untrusted | `gmdr-import --list-pakfile` | same sandbox; 512 MB cap; ZIP directory only |
| `state.gmstate` written by the importer | untrusted (the importer may be compromised) | main process | magic/version, every chunk: sizes ≤ 256 MB **and inside the file before allocating**, XXH3, exact zstd frame size; every record bounds-checked; fuzzed (`fuzz_statedb`) |
| Workshop GMAs, VPKs, VDF configs of the local install | semi-trusted (GMAs come from the Workshop) | main process | header/table parsing only, bounds-checked, growing reads capped at 64 MB, fuzzed (`fuzz_archives`); paths with `..` or `:` never resolve |
| UI → engine (`engine_call`) | trusted code, local content only | main process | CSP `default-src 'self'`, `freezePrototype`, no remote URLs, capabilities: window buttons + open dialog only; no shell/fs plugins. Smoke test confirms a remote `fetch` is blocked |
| Paths into the engine | from the open dialog or drag & drop | `demo.open` requires an absolute path; the file is hashed in the main process (read-only map) and parsed only in the importer |

## Findings

| # | Severity | Finding | Status |
| --- | --- | --- | --- |
| S1 | Medium | `gmdr-import` runs as the user without AppContainer: an exploited parser could open files by path (the Job Object limits resources, not file access) | **Open — owner decision.** AppContainer needs a per-user profile (registry) and read/execute for ALL APPLICATION PACKAGES on the importer's folder |
| S2 | Medium | Allocation before validation: LZSS reserved the declared size (≤ 64 MB) from a tiny input; zstd allocated the declared size (≤ 256 MB) before checking the frame; a state chunk's stored size was allocated before checking the file length | **Fixed** (ratio bound, `ZSTD_getFrameContentSize`, file-size check) + limit tests |
| S3 | Low | `BitReader::bytes`: `n * 8` bound check could overflow; zero-length read passed null to `memcpy` (UBSan) | **Fixed** + regression test |
| S4 | Low | SendTables element check indexed `props[-1]` for an excluded Array-typed prop (found by CI fuzzing) | **Fixed** + regression test |
| S5 | Low | VPK archive name formatted into an 8-byte buffer (truncation with ≥ 4-digit indices) | **Fixed** |
| S6 | Info | ASan builds skip `RLIMIT_AS` for the child on Linux (ASan cannot run under it) | Accepted: CI-only builds; release builds keep the cap |
| S7 | Info | WebView2 honours `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` (used by the smoke test to open a debugging port): any process of the same user could set it | Accepted: requires local code execution as the user already |
| S8 | Low | CI uses third-party actions by tag (`@v4`, `@v1`, `@v2`) | Recommend pinning to commit SHAs before the repository becomes public |
| S9 | Info | Dev bridge (`gmdr-cli serve` + `dev-bridge.mjs`) exposes the engine over HTTP | Dev only, bound to 127.0.0.1, not part of any build |

## Privacy
- Demos and state files never enter the repository (`*.dem`, `*.gmstate` ignored); corpus summaries hold counts and hashes only.
- Player names and SteamIDs stay in the local cache and the UI; log lines carry file names and error codes, not player data. No telemetry.
- Settings keep the recent-demos list (local paths) in `%APPDATA%\DemoGmodRender\settings.json`.

## Supply chain
vcpkg baseline pinned; npm dependencies pinned exactly with a lockfile; `Cargo.lock` committed; Anvil vendored with its licences (IBM Plex OFL, Fluent icons MIT).

## Verdict
No open high-severity issue. Ship-blocking only if the owner requires AppContainer (S1) for this sub-project; otherwise S1 moves to the next one with the decision recorded.
