# DBA — app-shell-demo-import: `state.gmstate`

Format version 1 (ADR-002 as amended by ADR-005). A cache, never user data: another format or parser version → rebuild.

## Layout
- 64-byte header: `GMSTATE\0`, format version, flags (bit 0 = complete), BLAKE3 of the .dem, parser version, directory offset.
- Append-only chunks: 40-byte header (magic `CHNK`, kind, flags, tickFrom, tickTo, raw size, stored size, XXH3-64 of the stored bytes) + zstd payload.
- Order: INFO (on ServerInfo) → INDEX → SCHEMA → per keyframe: DELTAS(prev segment) → KEYFRAME → STRINGTABLES (+ EVENTS, CAMERA of the segment) → at the end LIVES → MANIFEST → INFO (final) → DIRECTORY; then the header is rewritten with the complete flag.
- Segments: DELTAS/EVENTS/CAMERA chunk with `tickFrom = k` holds records with tick in (k, next keyframe]; the first segment starts at −1. Record ticks are deltas from the previous record, starting at `tickFrom`.

## Integrity
- Every chunk: magic, kind and sizes match the directory entry, sizes ≤ 256 MB, checksum, exact zstd size.
- Every record is bounds-checked (entity index < 8192, class id < schema, prop index < class props, string/userdata limits). Damage → `statedb.corrupt`, never a crash; checked by a 60-mutation test.

## Readers
- Random access: nearest keyframe with known STRINGTABLES + that segment's DELTAS up to T. Keyframe states (4) and composed tables (8) are cached; decoded chunks in a 512 MB LRU.
- Forward playback: the cursor continues from the last tick in the same segment.
- Entity life uid = `stableId64(demo BLAKE3 ‖ index, serial, firstTick, classId)`.
