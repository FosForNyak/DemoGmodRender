# Status

**Phase:** 4 — Implementation (T1–T13 done; next T14 assets); phases 0–3 complete
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

## Spike findings (GMod protocol 24) — COMPLETE
Both local demos decode fully to entity state with `.10x/spikes/demo-format/ref.py` (reference decoder):
- 29_08_2026.dem: 215 740 packets, 15 530 enters, 7.57 M deltas, 0 errors.
- 25_07_2025.dem: 148 767 packets, 9 145 enters, 6.28 M deltas, 1 error (known odd signon packet 3).
All format facts are in spec §4 (UBitVar low-bit encoding, 5-bit log2 max, 19-bit userdata length, varint ints, type 3 = double, type 7 = NW2 table, truncated dem_stringtables).

## Phase 4 progress
- T1–T10 (commit ec89876): repo scaffolding, `core`, `demo` parser. C++ output equals `ref.py` on both demos.
- T11–T13: state file writer/reader (`engine/demo/statedb/`), `demo/import` pipeline, `gmdr-import` child (inherited handles, Job Object 4 GB, mitigation policies, JSON lines), `gmdr-cli import|spawn-import|verify|inspect`. Verified on both demos: 0 mismatches, ratio 0.28/0.34, import 6.5 s for 183 MB, seek ≤ 26 ms. See ADR-005 implementation notes.
- Tests: 30 cases / 28 808 assertions (debug + release).

## Next
- T14–T15 assets (VDF, Steam/GMod locator, VPK, GMA, ZIP, VFS, content check) → T16–T17 api/cli → T18–T22 app → T23–T24 hardening + CI.

## Inputs
- Engine architecture: `C:\Users\ilomi\Downloads\Архітектура рушія.md` (also Claude Doc "Рушій рендеру демок Garry's Mod: архітектура", tabs: Архітектура, Слабкі місця, NW / NW2, Стрес-тест, Оптимізація)
- Design system: Anvil (claude.ai design-system artifact) — React 18 bundle `window.Anvil`, IBM Plex, dark graphite + amber accent; tokens.css is not published and must be generated from tokens.json

## Environment found
- VS 18 (bundled CMake/Ninja), Node 26, Rust/cargo, Python 3.14, git, gh, WebView2 runtime 154
- GMod at C:\Games\steamapps\common\GarrysMod; local sample demos (GMODEMO, demo protocol 3, net protocol 24, map gm_alium_nook, 109 MB and 183 MB, 33 and 66 tick). Not for the repo: they contain other players' data.

## Blockers
- None
