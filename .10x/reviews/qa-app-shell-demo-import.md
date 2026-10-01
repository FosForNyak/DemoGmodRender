# QA review — app-shell-demo-import

**Date:** 2026-10-01 · **Branch:** `feat/app-shell-demo-import` (PR #1 → `beta`)

## Gate (spec §13)

| Item | Result | Evidence |
| --- | --- | --- |
| Both local demos import fully; only the documented signon decode error | **Pass** | 25_07: 1 × `demo.message_truncated` (signon packet 3); 29_08: 0. Corpus summaries in `engine/tests/corpus/` |
| Player positions continuous (no jump > 1000/tick except respawns) | **Pass, one reviewed exception** | `gmdr-cli gate`: 25_07 0 unexplained; 29_08 1 — player #5 at tick 184 668 stood still ~220 s, then appeared 1 532 units away falling (`FL_ONGROUND` cleared, vz −12, alive): an in-game teleport. ~480 seat/vehicle "jumps" are relative origins (`moveparent`), handled |
| userinfo players = player entities in the recorder's PVS at the start ± players outside PVS | **Pass** (after a fix) | 25_07: 18 = 18; 29_08: 13 vs 12 (+1 outside PVS). Before the fix, players connected before recording had no userinfo (snapshot was ignored) |
| 183 MB import ≤ 15 s; seek ≤ 100 ms; UI responsive during import | **Pass** | 6.5 s in-process / 6.4 s via `gmdr-import`; seek avg 9–16 ms, max 26 ms; frame step ≤ 8 ms. Queries answer during import (readyTick advances, cancel works) |
| Content report finds `gm_alium_nook` and lists what is missing with reasons | **Pass** | Map in a Workshop GMA; missing ≈ 70 custom sounds + addon 129739986 (only `_legacy.bin`); statuses shown with icon + word |
| App opens, Anvil look in dark and light, all actions from the keyboard | **Pass** | Smoke test of the release window; screenshots in both themes; every command is in the palette (Ctrl+Shift+P) with shortcuts. Title-bar menus by keyboard: Alt or F10 focuses «Файл», arrows open and walk the menus, a second Alt/F10 or Esc returns focus |
| CI green on Windows and Linux; 60 s fuzzing per target without crashes | **In progress** | Locally: MSVC Debug/Release, Linux GCC, Linux Clang ASan/UBSan all green; 60 s × 4 targets clean in Docker. First CI run found one bug (fixed, regression test); re-run pending |

## Test inventory (spec §12)

| Layer | What exists |
| --- | --- |
| Unit (doctest, 50 cases) | BitReader, ByteReader, hashes, zstd, UTF-8, paths, files; header, command walk, string tables (+ snapshot), LZSS, SendTables flatten order, props, coordinates, timeline clock; statedb round trip, streaming, corruption, index; limits ("bombs"); VDF, VPK, GMA, ZIP, BSP, locator/VFS/content on a fake install; API (errors, settings, real import through `gmdr-import`, cache, C ABI) |
| Fuzz | `fuzz_demo`, `fuzz_tables`, `fuzz_archives`, `fuzz_statedb`: libFuzzer + ASan/UBSan (Clang) starting from each target's seeds, mutation driver as ctest (MSVC, GCC). `fuzz_demo`: 69 → ~11 800 inputs/s, coverage 1 349 (60 s, empty corpus) → 2 559 (30 s, seeded) |
| Corpus (local) | `GMDR_CORPUS=<dir>` → `demo_corpus_test` against `tests/corpus/*.summary.json`; `gmdr-cli verify` (state file vs parser, 0 mismatches); `gmdr-cli gate` |
| UI | `vitest` (formatting); `tsc` strict; browser run through the dev bridge; `smoke-app.mjs` on the release window |
| Rust | `cargo test` in CI: the FFI wrapper's calls, error mapping, event delivery and calls from several threads; the smoke test drives it in the real window |

## Manual checks done (browser + real engine, both demos)
Open from recent list, cached reopen, Outliner (names from userinfo, groups, search, selection), Inspector (props by SendTable, "changed in this tick", search, Змінені filter), top-down view (world grid and axes, selection highlight, zoom/pan, recorder camera), playback at 33 tick/s, scrubbing, timeline window with event tracks and the selected entity's PVS clips, content table and filters, settings (theme, density, motion, GMod path, cache), command palette, toasts, both themes.

## Issues found and fixed during QA
1. Outliner stayed empty: Anvil `TreeView` keeps its first `items` (remount keyed by content).
2. Duplicate toasts: engine event subscription doubled by React StrictMode (unsubscribe on cleanup).
3. Cached demos skipped the content check (no `import.done` event).
4. Viewport grid suggested a false world origin (own world-aligned grid).
5. Canvas colours stale after a theme switch (redraw on `data-theme`).
6. Timeline ruler labels overlapped at 20 s windows (10 s window).
7. Seated players at (0,0,0) and attached weapons at relative positions (`moveparent`).
8. Missing player names (dem_stringtables snapshot).

## Open / not covered
- Settings is a `ToolWindow` over the main window, not a separate OS window.
- No automated UI interaction tests (Playwright on the dev bridge would be the next step).
