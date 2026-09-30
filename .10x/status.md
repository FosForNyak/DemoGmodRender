# Status

**Phase:** 3 — Planning complete; Phase 4 not started (spike on demo format in progress)
**Updated:** 2026-09-30
**Feature slug:** `app-shell-demo-import`

## Decisions from brainstorming (user answers, 2026-09-30)
| Question | Decision |
| --- | --- |
| First sub-project scope | Anvil app shell + .dem import: header, fast index, string tables, classes and entities, timeline events, content manifest vs GMod install. Viewport is a placeholder; RHI/viewport is the next sub-project |
| UI host | Tauri 2 (Rust) + C++20 engine |
| Rust/C++ boundary | Engine in-process as a static lib behind a C ABI (JSON command bus); .dem parsing in a separate child process `gmdr-import` |
| Platforms | Windows app; C++ core and parser tests also built and run on Linux in CI (GCC + Clang, ASan/UBSan) |
| Git | Branch `feat/app-shell-demo-import`, PR for review; commits with the noreply address |

## Design presented so far (part 1 of 3, not yet approved)
- One app process (WebView2 + Anvil → Rust/Tauri → C++ core: core → demo → assets → api); importer child gets only inherited handles, Job Object limits, AppContainer as the last task.
- Command bus shared by UI, CLI and future scripting API.
- 64-bit entity life IDs, int64 ticks, flicks for timeline, x64 only.
- Parsers stay in C++ (Rust-parser option from "Слабкі місця" noted and declined).

## Done (2026-09-30)
- Spec: `.10x/specs/2026-09-30-app-shell-demo-import-design.md` (user gave standing permission to skip section approvals)
- Phase 1: cto/, product-manager/ · Phase 2: ADR-001…004, architect/, staff-engineer/ · Phase 3: engineering-manager/ (T1–T24), senior-engineer/
- Spike scripts: `.10x/spikes/demo-format/` (Python, run against local demos; no demo data in repo)

## Spike findings (GMod protocol 24)
- Command framing, ServerInfo (+loadingurl, gamemode, 16 bits), CreateStringTable (**5-bit log2 max**, numEntries, varint length), PacketEntities 13/24 bits, msg 33 (20-bit len): confirmed; demo 2 walks 215 740 packets with 0 errors.
- All signon string tables decode to the exact bit; compressed tables are Valve LZSS; `dem_stringtables` is truncated at 512 KB (last table only).
- SendTables: 16-bit flags, 311 tables, 250 classes, exact.
- Prop indices: TF2 style `while bit: idx += 1 + UBitVar`. Ints with flag 0x20 are **varints** (CS:GO SPROP_VARINT).
- **Open:** 8/34 baselines decode exactly. `DT_BaseEntity` has prop `m_GMOD_DataTable` of **type 7** (bits 0) right before the `m_GMOD_*` var tables — it is decoded as Int64 now and is the prime suspect for the remaining misalignment (needs its own decoder, likely length-prefixed). Verify flatten order after fixing it (`trace2.py`, `run.py`, `flatvar.py`).

## Next
1. Finish the spike: decoder for prop type 7, all 34 baselines exact, then `ents.py` over 3000 packets (player positions sane).
2. Update spec §4 with the findings, then Phase 4 from T1.

## Inputs
- Engine architecture: `C:\Users\ilomi\Downloads\Архітектура рушія.md` (also Claude Doc "Рушій рендеру демок Garry's Mod: архітектура", tabs: Архітектура, Слабкі місця, NW / NW2, Стрес-тест, Оптимізація)
- Design system: Anvil (claude.ai design-system artifact) — React 18 bundle `window.Anvil`, IBM Plex, dark graphite + amber accent; tokens.css is not published and must be generated from tokens.json

## Environment found
- VS 18 (bundled CMake/Ninja), Node 26, Rust/cargo, Python 3.14, git, gh, WebView2 runtime 154
- GMod at C:\Games\steamapps\common\GarrysMod; local sample demos (GMODEMO, demo protocol 3, net protocol 24, map gm_alium_nook, 109 MB and 183 MB, 33 and 66 tick). Not for the repo: they contain other players' data.

## Blockers
- None
