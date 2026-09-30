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
| T16 | `api`: Engine, command table, sessions (importer supervision, events, watchdog, cancel), settings, GMod/VFS cache, demo cache with LRU limit, C ABI `gmdr.h` | `api_engine_test.cpp` (synthetic demo through gmdr-import, cached reopen, C ABI); `gmdr-cli run` on both demos |
| T17 | `gmdr-cli call / run [--no-wait] / info` over the same command table | corpus runs, cancel during import |
| T18 | Tauri 2 shell (`app/src-tauri`): `build.rs` builds the engine via `scripts/build-engine.cmd`, links the static libs, ships `gmdr-import` as a sidecar; FFI over `gmdr.h`; `engine_call` command + `engine` event; frameless window, CSP, minimal capabilities | `cargo build`, `tauri build --no-bundle`, `ui/scripts/smoke-app.mjs` |
| T19–T22 | UI (`app/ui`): Vite + React 18 + TS, vendored Anvil + `tokens.css` generator, Workspace with TitleBar/menus, toolbar with transport, Outliner, top-down view, Inspector, Scrubber + Timeline, Content table, Log, StatusBar, Settings, command palette, toasts, drag & drop, shortcuts, Ukrainian text | typecheck, vitest, browser run on both demos through the dev bridge (screenshots in both themes) |
| T23 | Fuzz targets (`engine/fuzz`: demo pipeline, table decoders, archives/VDF, state reader) — libFuzzer with Clang, a mutation driver as ctest elsewhere; limit tests (`demo_limits_test.cpp`); sizes checked before allocating (LZSS ratio bound, zstd frame size, chunk inside the file) | ctest on MSVC Debug and Linux GCC; Clang ASan/UBSan + libFuzzer in Docker (`engine/docker`) |
| T24 | GitHub Actions (`.github/workflows/ci.yml`): MSVC Debug, Linux GCC, Linux Clang ASan/UBSan + 60 s fuzzing per target, Tauri app + NSIS installer as an artifact (not published); `README.md`; `scripts/build-engine.cmd` picks one VS instance (full edition over Build Tools) and always configures | local runs of the same presets |
| T15 | `assets`: VPK v1/v2, GMA v1–3 (header read in growing chunks), ZIP central directory, BSP lump 40, VFS (folder → addons → Workshop GMAs → VPKs → mounts → download → pakfile), content check | unit tests + corruption runs; `gmdr-cli content` on both demos |

## Deviations from the plan
- Row-oriented deltas instead of columnar (ADR-005).
- `TimelineClock`: signon → tick 0, monotonic timeline (found by `verify`).
- STRINGTABLES snapshots are incremental per table; `instancebaseline` userdata not stored (ADR-005 notes).
- `gmdr-import` sets process mitigation policies (no dynamic code, no remote/low-IL images, no extension points, strict handle checks after validating the inherited handles). AppContainer remains T23.

## UI notes
- **Dev bridge** (`gmdr-cli serve` + `app/ui/scripts/dev-bridge.mjs`, dev only, 127.0.0.1): the same UI runs in a browser tab against the real engine, so it can be checked with screenshots outside the Tauri window.
- **Anvil TreeView keeps `items` from its first render**: the Outliner remounts it (key = hash of the visible nodes). Other components only snapshot `default*` props.
- **Viewport grid** is decorative and centred on the box, so the top-down view hides it and draws a world-aligned grid and axes (tokens `grid-*`, `axis-*`); colours are re-read when the theme changes.
- **Timeline**: `Scrubber` over the whole demo (`cached` = imported range) + `Timeline` for a 10 s window around the playhead (events of that window as keys per kind, the selected entity's PVS intervals as clips).
- **Settings** is a `ToolWindow` over the main window (not a separate OS window yet); changes apply immediately.
- A cached demo starts the content check right after opening (no `import.done` event).

## API v1 as implemented
Request `{"cmd", "args"}` → `{"ok": true, "result"}` or `{"ok": false, "error": {code, message, details?}}`. Events `{"type", ...}`: `import.indexed`, `import.progress`, `import.done`, `import.failed`, `settings.changed`, `log`.

| Command | Args | Notes |
| --- | --- | --- |
| `app.info` | — | version, format/parser versions, dirs, importer found |
| `settings.get` / `settings.set` | `key?` / `key, value` | known keys `gmod.path`, `cache.limitBytes`, `recent`; any `ui.*`; null removes |
| `gmod.locate` | `refresh?` | install, mounts, counts; VFS cached until `gmod.path` changes |
| `cache.info` / `cache.clear` | — | open demos are never removed |
| `demo.open` | `path` (absolute) | same file → same session; cached state reused |
| `demo.close` / `demo.list` / `demo.info` | `demo` | info adds header, server, tick rate, duration |
| `import.cancel` | `demo` | partial state stays readable |
| `state.entities` | `demo, tick, filter?{text, group, inPvs}` | uid (string), index, class, group, life, inPvs, name/bot (players), model |
| `state.entity` | `demo, tick, uid|index` | props grouped by SendTable, `changed` flags |
| `state.positions` | `demo, tick` | unparented entities with origin + yaw; players with pitch/name; recorder camera |
| `entity.lifetime` | `demo, uid` | after the import |
| `timeline.summary` | `demo, bins` | per-kind histograms; cached once complete |
| `timeline.events` | `demo, from, to, kinds?, limit?` | ≤ 20 000, `truncated` flag |
| `camera.track` | `demo, from, to, step` | compact rows `[tick, x, y, z, pitch, yaw, roll]` |
| `content.check` | `demo` | after the import; pakfile listed in gmdr-import |

`tick` is clamped to the imported range; before the first delta chunk state queries return `import.not_ready`. Player names come from `userinfo` (GMod `player_info_t`, 324 bytes, name[128]).

## Content check rules
- `*N` models = map brush models (builtin); `.vmt`/`.spr` in modelprecache → `materials/`.
- Sounds: Source prefix chars stripped, `sound/` prepended; `!name` = sentence (builtin); no `.wav/.mp3/.ogg` extension = soundscript entry (not checked).
- Decals → `materials/<name>.vmt`. Downloadables `<id>.gma` = Workshop items: installed GMA / legacy `_legacy.bin` / missing.
- Particles are effect names (not checked until PCF parsing exists).
- VFS rejects paths with `..` or `:`.

## Open
- **AppContainer for gmdr-import (T23, needs the user's decision):** it requires a per-user AppContainer profile (registry) and read/execute for ALL APPLICATION PACKAGES on the importer's folder. Not done without explicit consent; the importer runs in the Job Object with mitigation policies meanwhile.
- Property-history queries (graphs) will need a derived index; not in this sub-project.
- XOR-with-previous encoding for float updates could shrink DELTAS further; not needed for the 0.4 target.
