# CTO — app-shell-demo-import

**Date:** 2026-09-30

## Problem
The old approach (GMod `startmovie`) caps output at ~30 fps and depends on the game client. The new engine needs a desktop application as its face, and the first thing it must prove is that we can read GMod demos ourselves — every later phase (render, video, editing) stands on the demo parser and the state base.

## Verdict: build
- **Build vs buy.** No existing tool parses GMod protocol-24 demos into a queryable timeline (GMod-specific CreateStringTable/ServerInfo/net messages confirmed by spike). Source demo parsers for TF2/CS exist but none match GMod's variant, and none give an editor UI. Build.
- **Scope choice.** App shell + .dem import first (user choice). Rendering is the next sub-project; this order de-risks the data side, which the architecture doc names as the biggest risk ("найбільший ризик — не графіка, а дані").
- **Tech direction.**
  - Tauri 2 (user choice) over native WebView host: accepts Rust in the build in exchange for a mature shell (window, packaging, updater later).
  - Engine stays C++20 per the architecture doc; Rust only hosts the window and forwards commands.
  - Engine in-process behind a C ABI; import in a child process (user choice) — the split the "Слабкі місця" tab recommends.
  - Windows app now; core + parser tests on Linux CI so the core stays portable.

## Opportunity cost
Rendering work waits one sub-project. Accepted: a renderer without trustworthy scene data can't be verified against GMod anyway.

## Risks the CTO tracks
| Risk | Mitigation |
| --- | --- |
| GMod protocol variants change silently | variant table + autodetect + local corpus summaries |
| Parser memory-safety bugs on hostile demos | child process + Job Object, bounds-checked readers, fuzzing |
| Two languages (Rust + C++) | Rust layer kept thin: window, dialogs, FFI forwarding only |
