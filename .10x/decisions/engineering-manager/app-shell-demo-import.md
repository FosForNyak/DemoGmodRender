# Engineering Manager — app-shell-demo-import

**Date:** 2026-09-30

## Milestones
| M | Name | Tasks | Exit criterion |
| --- | --- | --- | --- |
| M1 | Engine skeleton builds | T1–T3 | CMake preset builds core + tests on Windows; `ctest` green |
| M2 | Demo decodes | T4–T10 | corpus: both demos decode; player positions continuous |
| M3 | State base | T11–T13 | import writes `state.gmstate`; reader answers `state_at` ≤ 100 ms |
| M4 | Content | T14–T15 | content check finds `gm_alium_nook` and lists missing items |
| M5 | API + CLI | T16–T17 | `gmdr-cli info/import/query` works on corpus |
| M6 | App | T18–T22 | Tauri app opens a demo; all panels work |
| M7 | Hardening + CI | T23–T24 | fuzz targets, CI green Windows + Linux, AppContainer |

## Tasks (in order; dependencies are the previous task unless noted)
| # | Task | Est. | Depends |
| --- | --- | --- | --- |
| T1 | Repo scaffolding: CMake presets (windows-msvc, linux-gcc, linux-clang-asan), vcpkg.json, clang-format, .gitignore, README | 2 h | — |
| T2 | `core`: Error/Result, ByteReader, BitReader (ubit, sbit, varint32, UBitVar, strings, floats, coord/coordMP/normal), limits | 3 h | T1 |
| T3 | `core`: hashing (BLAKE3, XXH3), zstd wrapper, file IO (positional read, mmap), JSON, log; unit tests | 3 h | T2 |
| T4 | `demo`: header + command walk + fast index; tests with synthetic files | 2 h | T3 |
| T5 | `demo`: net message framing + ProtocolVariant (gmod-2025) + all message readers; unknown → decode_error | 4 h | T4 |
| T6 | `demo`: string tables (create/update, history, fixed userdata, LZSS decompress), dem_stringtables snapshot | 3 h | T5 |
| T7 | `demo`: SendTables parse + flatten (excludes, collapsible, CHANGES_OFTEN swap) + class list | 3 h | T5 |
| T8 | `demo`: prop decoders (int, int64, float variants, vector, vectorxy, string, array) | 3 h | T7 |
| T9 | `demo`: PacketEntities state machine (enter/leave/delete, serials, instance + dual entity baselines, explicit deletes) | 4 h | T6, T8 |
| T10 | `demo`: events (game event list + decode, temp ents, sounds, net 33 names, user/entity msgs, decals, setview, fixangle, consolecmd) + camera; corpus summary test | 4 h | T9 |
| T11 | `demo/statedb`: writer (chunks, keyframes, deltas, events, camera, lives, manifest, directory) | 4 h | T10 |
| T12 | `demo/statedb`: reader (validation, LRU, state_at, stepping, lifetimes, events range) + roundtrip tests | 4 h | T11 |
| T13 | `gmdr-import` tool + progress protocol; `core/process` spawn with inherited handles + Job Object | 3 h | T12 |
| T14 | `assets`: VDF, Steam/GMod locator, mount.cfg | 2 h | T3 |
| T15 | `assets`: VPK, GMA, ZIP (pakfile via importer manifest), VFS order, content check | 4 h | T14 |
| T16 | `api`: Engine, sessions, command registry, all v1 commands, event sink, C ABI `gmdr.h` | 4 h | T13, T15 |
| T17 | `gmdr-cli` | 1 h | T16 |
| T18 | Tauri app scaffold: frameless window, build.rs CMake link, FFI wrapper, engine_call, events, dialog, drag&drop | 3 h | T16 |
| T19 | UI scaffold: Vite + React 18 + TS, Anvil vendoring, tokens.css generator, theme/density, i18n, engine.ts | 3 h | T18 |
| T20 | UI: Workspace, TitleBar + menus, Toolbar/transport, StatusBar, start screen, command palette, settings window | 4 h | T19 |
| T21 | UI: Outliner, Inspector, Timeline (tracks, entity lifetime, import progress), Content, Log | 4 h | T20 |
| T22 | UI: top-down view (canvas), playback loop, keyboard; manual app run + screenshots | 3 h | T21 |
| T23 | Fuzz targets (libFuzzer), limits "bomb" tests, AppContainer for importer | 3 h | T16 |
| T24 | GitHub Actions CI (Windows + Linux), packaging job (no publish); docs | 2 h | T23 |

Total ≈ 74 h of focused work.

## Risks & unknowns (front-loaded)
- T7–T9 carry the unverified parts (flag width confirmed 16; flatten order and prop index encoding unconfirmed) → done early, validated on corpus before building storage.
- T18 Tauri + CMake link on MSVC → spike first thing in M6.
- CI Linux runner: vcpkg build time → cache vcpkg binary packages.
