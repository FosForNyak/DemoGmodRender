# Status

**Phase:** 0 — Brainstorming (design part 1 of 3 presented, awaiting approval)
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

## Next
- Design part 2: import pipeline, state DB format, content check
- Design part 3: UI screens, errors, security, testing, CI
- Then: spec in `.10x/specs/`, spec review, Phase 1

## Inputs
- Engine architecture: `C:\Users\ilomi\Downloads\Архітектура рушія.md` (also Claude Doc "Рушій рендеру демок Garry's Mod: архітектура", tabs: Архітектура, Слабкі місця, NW / NW2, Стрес-тест, Оптимізація)
- Design system: Anvil (claude.ai design-system artifact) — React 18 bundle `window.Anvil`, IBM Plex, dark graphite + amber accent; tokens.css is not published and must be generated from tokens.json

## Environment found
- VS 18 (bundled CMake/Ninja), Node 26, Rust/cargo, Python 3.14, git, gh, WebView2 runtime 154
- GMod at C:\Games\steamapps\common\GarrysMod; local sample demos (GMODEMO, demo protocol 3, net protocol 24, map gm_alium_nook, 109 MB and 183 MB, 33 and 66 tick). Not for the repo: they contain other players' data.

## Blockers
- None
