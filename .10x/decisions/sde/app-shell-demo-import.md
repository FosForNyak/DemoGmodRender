# SDE — app-shell-demo-import

## Done
| Task | Result | Verified by |
| --- | --- | --- |
| T1 | CMake presets (MSVC debug/release, GCC, Clang+ASan), vcpkg manifest (baseline pinned), `.clang-format`, `.gitattributes` (LF) | builds with `/W4 /WX` |
| T2–T3 | `core`: errors, limits, bit/byte readers, BLAKE3/XXH3, zstd, positional file IO + mmap, UTF-8 paths, child process with Job Object | unit tests |
| T4–T10 | `demo`: header, commands, LZSS, string tables, SendTables + SDK 2013 flattening, props (incl. NW2), game events, parser with dormant entities | equals `ref.py` on both demos |
| T11–T12 | `demo/statedb`: codec, writer, reader (checksums, LRU, keyframe cache, forward cursor, composed string tables) | round-trip, streaming and corruption tests; `gmdr-cli verify` on both demos |
| T13 | `demo/import` (pass 1 index + pass 2 decode), `tools/gmdr-import` | `gmdr-cli spawn-import` on both demos; bad-handle and usage errors produce an `error` line |
| T14 | `assets`: VDF (escapes only for Steam-written files), locator (registry → libraryfolders → appmanifest_4000 → GarrysMod; manual path; mount.cfg + mountdepots.txt with dedupe) | unit tests on a fake install; `gmdr-cli gmod` on this PC |
| T15 | `assets`: VPK v1/v2, GMA v1–3 (header read in growing chunks), ZIP central directory, BSP lump 40, VFS (folder → addons → Workshop GMAs → VPKs → mounts → download → pakfile), content check | unit tests + corruption runs; `gmdr-cli content` on both demos |

## Deviations from the plan
- Row-oriented deltas instead of columnar (ADR-005).
- `TimelineClock`: signon → tick 0, monotonic timeline (found by `verify`).
- STRINGTABLES snapshots are incremental per table; `instancebaseline` userdata not stored (ADR-005 notes).
- `gmdr-import` sets process mitigation policies (no dynamic code, no remote/low-IL images, no extension points, strict handle checks after validating the inherited handles). AppContainer remains T23.

## Content check rules
- `*N` models = map brush models (builtin); `.vmt`/`.spr` in modelprecache → `materials/`.
- Sounds: Source prefix chars stripped, `sound/` prepended; `!name` = sentence (builtin); no `.wav/.mp3/.ogg` extension = soundscript entry (not checked).
- Decals → `materials/<name>.vmt`. Downloadables `<id>.gma` = Workshop items: installed GMA / legacy `_legacy.bin` / missing.
- Particles are effect names (not checked until PCF parsing exists).
- VFS rejects paths with `..` or `:`.

## Open
- Property-history queries (graphs) will need a derived index; not in this sub-project.
- XOR-with-previous encoding for float updates could shrink DELTAS further; not needed for the 0.4 target.
