# Senior Engineer — app-shell-demo-import

**Date:** 2026-09-30

## Approach per task
- **T2 BitReader:** 64-bit refill window over a `std::span<const uint8_t>`; `ubit(n)` for n ≤ 32 in O(1); reading past the end returns 0 and sets `overflow_`; `varint32` max 5 bytes; strings with explicit max length (truncate + flag). Coord/CoordMP/Normal decoders are free functions over BitReader, mirroring SDK 2013 behavior (spec §4).
- **T4 command walk:** header 1072 bytes; each command: u8 cmd, i32 tick; packet: 76 + 8 + i32 len. Validate `len ≥ 0` and `len ≤ remaining`. Unknown cmd → stop with `demo.unknown_command` and keep what was read.
- **T5 net messages:** one `readMessage(type)` switch; widths from `ProtocolVariant`. GMod variant confirmed by spike: ServerInfo + loadingurl + gamemode + 16 bits; CreateStringTable with 4-bit log2 max, extra bit, varint length; PacketEntities 13/24 bits; msg 33 with 20-bit length. On failure inside a packet: emit `decode_error` with packet offset + bit offset + message type, skip the rest of the packet.
- **T6 string tables:** entry decode per SDK 2013 (sequential index bit, substring history of 32, fixed userdata bits / 14-bit byte count). Compressed data: header `uint32 decompressedSize, uint32 compressedSize` then Valve LZSS (`"LZSS"` magic) — implement decompressor with output cap.
- **T7 flatten:** gather excludes (table, prop) recursively; build hierarchy: iterate props; `EXCLUDE`/`INSIDEARRAY` skipped; DataTable → collapsible: inline, else recurse first; then own non-DT props appended; finally SDK 2013 single-pass swap of `CHANGES_OFTEN` to the front. Array element = prop before the array.
- **T8 decoders:** per spec; Int64 prop type 7 exists in GMod (one prop): decode as signed (sign bit + 32 + (bits−33)) / unsigned (32 + (bits−32)) — verify against corpus value sanity.
- **T9 entities:** header delta via UBitVar; flags leave/enter/delete; enter reads class (log2(nclasses)+1 bits) and serial (10 bits); baseline selection: entity baseline set `baseline` bit for this index if class matches, else instance baseline for class (decoded lazily from `instancebaseline` string table, re-decoded when that entry changes). `updateBaseline` → copy entered entities' decoded state into the other baseline set. After updates, if delta: explicit deletes `while bit: index(13)`. Prop index stream: `while bit: idx += 1 + UBitVar`.
- **T11 writer:** keeps current state per live entity as column arrays per class; changes appended to per-class/per-prop builders; at each 30 s boundary flush `DELTAS` (previous interval) then `KEYFRAME` (current full state).
- **T12 reader:** directory from importer lines or `DIRECTORY` chunk; `stateAt(tick)`: nearest keyframe ≤ tick → copy → apply deltas ≤ tick; cache last result to step forward cheaply.
- **T13 process:** Windows `CreateProcessW` with `STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` (only the two handles + stdout pipe inherited), job with `JOB_OBJECT_LIMIT_PROCESS_MEMORY | KILL_ON_JOB_CLOSE | ACTIVE_PROCESS`, `CREATE_SUSPENDED` → assign → resume. POSIX: `posix_spawn` with fd actions + `setrlimit` (Linux CI).
- **T18 Tauri:** `build.rs` runs CMake (`cmake` crate) with preset `windows-msvc-release`, links `gmdr_api` + deps; `gmdr-import.exe` shipped via `bundle.externalBin`, located at runtime next to the app exe.

## Tricky parts
1. Exact SendTable flatten order (validated only by decoding real data).
2. Instance baselines arrive before datatables (signon order: stringtables in packet 2, datatables at tick 308) → decode lazily.
3. Progressive reads of a file being written by another process → only read chunks announced by the importer (fully flushed).
4. JSON through FFI must be UTF-8; player names can contain invalid UTF-8 → sanitize with replacement chars.
5. React 18 globals must exist before the Anvil bundle executes.
