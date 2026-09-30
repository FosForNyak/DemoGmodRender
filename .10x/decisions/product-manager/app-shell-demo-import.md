# Product Manager — app-shell-demo-import

**Date:** 2026-09-30 · Spec: `.10x/specs/2026-09-30-app-shell-demo-import-design.md`

## Problem statement
A creator has a 1-hour GMod demo and wants to know, without launching the game: what happened when, where every player was, what the recorder saw, and whether their machine has the content needed to render it later.

## User stories (MVP)
| # | Story | Acceptance |
| --- | --- | --- |
| U1 | Open a .dem via menu, Ctrl+O, drag & drop, or recent list | Demo header and duration visible within 1 s |
| U2 | See import progress and use the ready part immediately | Timeline full-length after the index pass; ready range grows |
| U3 | Select an entity and see its properties at the current tick | Values match the demo; out-of-PVS clearly marked |
| U4 | Scrub and play the demo | Seek ≤ 100 ms, playback step ≤ 16 ms |
| U5 | See events, entity lifetime and PVS gaps on the timeline | Tracks per event kind; selected entity track |
| U6 | Top-down map with players and the recorder's camera | Players move continuously during playback |
| U7 | Content report: what is missing and where the rest was found | Map found; missing list with reasons; Workshop IDs |
| U8 | Any command from the palette (Ctrl+Shift+P) and from `gmdr-cli` | Same command names in both |
| U9 | Open someone else's demo safely | Parser runs in a restricted child process |

## Out of scope
3D rendering, video output, editing layers/projects, audio playback, GLua, recording companion, Linux app, decoding NW/NW2/net message contents.

## Success criteria (gate)
Both local demos import fully (only the documented signon packet 3 undecodable), player positions continuous, 183 MB demo ≤ 15 s import, content report finds `gm_alium_nook`, app matches Anvil in dark/light, CI green on Windows + Linux.

## Priorities (MoSCoW)
- **Must:** U1–U4, U7, U9, CI.
- **Should:** U5 (entity lifetime track), U6 top-down view, U8 palette.
- **Could:** light theme polish, cache size meter, AppContainer.
- **Won't (this sub-project):** everything in "Out of scope".
