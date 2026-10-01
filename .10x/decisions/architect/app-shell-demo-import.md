# Architect — app-shell-demo-import

**Date:** 2026-09-30 · Spec: `.10x/specs/2026-09-30-app-shell-demo-import-design.md` · ADRs 001–004

## Components
| Component | Responsibility | Depends on | Interface |
| --- | --- | --- | --- |
| `core` | ByteReader/BitReader with bounds checks, errors (`Error{code,message}` + `Result<T>`), limits, BLAKE3, XXH3, zstd, positional file IO, mmap, process spawn + Job Object, JSON (nlohmann), logging | std, vendored libs | C++ headers |
| `demo` | header, command walk + fast index, netmsg decoding with `ProtocolVariant`, string tables (+LZSS), SendTables flatten, prop decoders, entity state machine (PVS, baselines), events, camera, statedb writer/reader | core | `DemoParser` (push-style callbacks), `StateWriter`, `StateReader` |
| `assets` | Steam/GMod locator (registry + VDF), VFS (dirs, VPK, GMA, ZIP pakfile), content check | core | `Vfs`, `GmodInstall`, `ContentChecker` |
| `api` | `Engine` (settings, open demos), command registry, C ABI, event sink, importer supervision | core, demo, assets | `gmdr.h` |
| `gmdr-import` | runs `DemoParser` → `StateWriter` on inherited handles, prints progress JSON lines | core, demo | argv + stdout protocol |
| `gmdr-cli` | runs commands without UI | api | argv |
| Tauri app (Rust) | window, dialogs, drag&drop, FFI wrapper, event forwarding | api (C ABI) | `engine_call`, window commands |
| UI (React + Anvil) | Workspace, panels, timeline, top-down view, settings, palette | Tauri `invoke`/events | — |

## Data flow
1. UI → `demo.open(path)` → engine hashes file (BLAKE3, streamed) → cache hit? open reader : spawn importer.
2. Importer: pass 1 (fast index) → `INDEX` chunk → `{"indexed":…}`; pass 2 → chunks every 30 s demo time → `{"chunk":…}` lines.
3. Engine supervisor thread reads lines → registers chunks with the demo's `StateReader` → pushes `import.progress` events.
4. UI queries (`state.entities`, `state.entity`, `timeline.*`) → `StateReader` → LRU of decoded chunks → materialized state at tick.

## Failure modes
| Failure | Behavior |
| --- | --- |
| Importer crashes / killed / exceeds memory | engine marks import failed at last good tick; ready chunks stay usable; UI InfoBar with "Повторити" |
| Undecodable packet | recorded as `EVENTS` entry `decode_error` (tick, offset, reason); parser resyncs at next packet |
| Truncated demo (no dem_stop) | import ends at last complete command; flagged "обрізане демо" |
| Corrupt chunk in state file | checksum mismatch → chunk ignored, re-import offered |
| GMod not found | content check disabled with InfoBar "Вказати шлях…"; demo still opens |
| Unknown protocol variant | autodetect fails → error `demo.unsupported_variant` with details for the log |

## Key trade-offs
- Positional reads + LRU instead of mmap of a growing file: simpler across processes on Windows, same performance at our chunk sizes.
- Keyframe interval 30 s (doc default); tuned later by measured seek time.
- Events reference `.dem` bits instead of copying: small state file, but reading payloads later requires the `.dem` (it is the source of truth anyway).
