# ADR-005: Row-oriented delta records inside state-file chunks

**Status:** Accepted (supersedes the "columnar" detail of ADR-002; the rest of ADR-002 stands)
**Date:** 2026-09-30
**Feature:** app-shell-demo-import
**Author:** 10x-Team (DBA + Senior Engineer)

## Context
ADR-002 specified `KEYFRAME`/`DELTAS` payloads "grouped by class, then by property (columns)". While implementing the writer against the real corpus it became clear that every read the UI needs is a replay: state at tick T = keyframe + all changes up to T, applied in order. Column groups have to be re-merged by tick for that, and the writer would have to keep per-class per-prop builders for 30 s of changes. The C++ parser emits ~7.6 M entity updates for a 1-hour demo in 1.4 s, so the writer must not become the bottleneck.

## Decision
Inside a `DELTAS` chunk, changes are stored as a sequence of records in demo order:
`op (u8) · Δtick (varint) · payload`, where op is ENTER (index, class, serial, life, full props), UPDATE (index, changed props), LEAVE (index, deleted), STRING (table entry change) or TABLE (string table created). Props are `Δindex (varint) · tagged value`. The chunk is zstd-compressed as a whole. `KEYFRAME` chunks hold the full state (all known entities with their set props) and are written every 30 s of demo time together with a `STRINGTABLES` snapshot.

## Alternatives Considered
| Alternative | Pros | Cons | Why Not |
|-------------|------|------|---------|
| Columnar per class/prop (ADR-002) | best compression; fast single-prop history | reads need a k-way merge by tick; complex writer | complexity without a current consumer (no curve/plot view yet) |
| One record per prop change (no grouping by entity) | simplest | repeats tick and entity for every prop | larger file |

## Consequences
### Positive
- Writer is a straight append; reader applies records in one pass.
- Same format serves the importer's progressive chunks and the final file.
### Negative
- History of one property (for future graphs) needs a scan of the delta chunks; acceptable, and a derived columnar index can be added later without changing this format.
### Risks
- File size: measured on the corpus in QA (target ≤ 0.4 × .dem).

## Implementation notes (T11–T13, measured on the two local demos)
- **Timeline ticks.** Signon packets and `dem_datatables` carry server ticks (96…304) unrelated to playback, which starts at demo tick 0. Everything before the first `dem_packet` is placed at tick 0 and the timeline is clamped to never go back (`TimelineClock`, used by the parser and the pass-1 index). Without this, states at ticks 0–70 did not match.
- **STRINGTABLES snapshots hold only tables changed since the previous snapshot**, each in full; the reader composes the latest version of every table ≤ keyframe (cached). Full snapshots were 9.3 MB of a 51 MB file; now 0.3 MB.
- **`instancebaseline` userdata is not stored** (the parser has already applied baselines); its strings are kept.
- **INDEX chunk** (pass 1, before decoding): tick → command offset, written first so the timeline has its full length within ~0.1 s.
- **zstd level 7 for DELTAS**, 3 elsewhere: −11 % size for +45 % import time.
- **Chunk order at a keyframe:** DELTAS (previous segment) → KEYFRAME → STRINGTABLES. The reader uses a keyframe only once its STRINGTABLES chunk is known.

| Demo | .dem | state | ratio | import (in-process / child) | seek avg / max | frame step max | states verified |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 29_08_2026 | 183 MB | 51.9 MB | 0.28 | 6.5 s / 6.4 s | 13 / 26 ms | 4 ms | 370 × 2, 0 mismatches |
| 25_07_2025 | 109 MB | 37.3 MB | 0.34 | 4.3 s / 4.5 s | 16 / 23 ms | 0.01 ms | 426 × 2, 0 mismatches |

`gmdr-cli verify <demo>` replays the parser's own entity and string-table state next to the importer and compares hashes at ~400 sampled ticks, in forward order (cursor) and in random order (keyframe + deltas). It also checks every 97th update that the parser's changed-prop list is complete.
