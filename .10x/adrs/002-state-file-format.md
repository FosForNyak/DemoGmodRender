# ADR-002: `state.gmstate` — append-only chunked file with keyframes and columnar deltas

**Status:** Accepted
**Date:** 2026-09-30
**Feature:** app-shell-demo-import
**Author:** 10x-Team (Architect + DBA)

## Context
A demo is a stream of deltas; the UI needs random access to any tick (seek ≤ 100 ms), sequential stepping (≤ 16 ms), and data while the import is still running. The architecture doc prescribes a timeline state base with periodic full snapshots ("як I-кадри у відео"), and the "Оптимізація" tab prescribes columnar storage with delta + zstd and ≤ 290 MB for a 6-hour demo. The importer runs in a separate process and must hand data to the main process progressively.

## Decision
One file per demo in the user cache (`%LOCALAPPDATA%\DemoGmodRender\cache\demos\<BLAKE3>\state.gmstate`):
- 64-byte header (magic, format version, flags incl. "complete", demo hash, parser version).
- Append-only chunks, each with a header (kind, tick range, raw/compressed size, XXH3-64 checksum) and a zstd payload.
- Chunk kinds: `INDEX`, `SCHEMA`, `KEYFRAME` (every 30 s of demo time), `DELTAS`, `EVENTS`, `CAMERA`, `STRINGTABLES`, `LIVES`, `MANIFEST`, `DIRECTORY` (last).
- Inside `KEYFRAME`/`DELTAS`: grouped by class, then by property (columns); ticks delta-varint, ints zigzag-varint, floats f32, strings via a per-chunk pool. Out-of-PVS = "unknown", never zero.
- Events store references (packet offset, bit offset, bit length) into the original `.dem` instead of copying payloads.
- Progressive reading: the importer prints each finished chunk's offset/size; the main process reads it with positional reads, verifies the checksum and caches the decoded chunk (LRU).

## Alternatives Considered
| Alternative | Pros | Cons | Why Not |
|-------------|------|------|---------|
| SQLite for the state base | transactions, ad-hoc queries | row-per-change is large and slow for millions of changes; WAL reader across processes while writing adds locking | keep SQLite for projects (later), not for bulk state |
| Full snapshot every tick | trivial reads | ~100× larger | size |
| Only keyframes + re-decode `.dem` between them | smallest | seeks re-run the parser in the main process (breaks the trust boundary) or IPC to the child | security + latency |
| Row-oriented deltas | simpler writer | compresses worse than columns | "Оптимізація" §1 chose columns |

## Consequences

### Positive
- Seek = one keyframe + one delta chunk; sequential play = incremental apply.
- The file is usable while being written; a crash leaves valid chunks.
- The `.dem` stays the source of truth; the state file is a disposable cache (version bump → rebuild).

### Negative
- Custom format: needs its own reader validation and fuzzing.
- Writer must buffer 30 s of changes per chunk.

### Risks
- Keyframe interval too coarse for fast seeks on 128-player servers → interval is a parameter; measure seek time on corpus.
- Checksum/format drift between importer and reader versions → parser and format versions in the header; mismatch triggers re-import.

## Dependencies
- ADR-001 (import in a child process).
- Later: the scene/sim sub-projects read entity state through the same reader API.
