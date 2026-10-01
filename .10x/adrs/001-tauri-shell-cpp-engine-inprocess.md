# ADR-001: Tauri 2 shell with the C++ engine in-process, import in a child process

**Status:** Accepted
**Date:** 2026-09-30
**Feature:** app-shell-demo-import
**Author:** 10x-Team (Architect + Staff Engineer)

## Context
The UI is built on the Anvil design system, which ships as a React 18 bundle (`window.Anvil`), so the interface runs in a web view. The engine is C++20 (architecture doc: Slang, Jolt, OIDN, FFmpeg are C/C++). Demos and Workshop content are untrusted input ("Слабкі місця", S1/S7: a crafted file must not give code execution). Timeline scrubbing issues many small state queries per second, and a later sub-project will attach a GPU viewport to the same window.

## Decision
- **Shell:** Tauri 2 (Rust) owns the window (frameless, Anvil `TitleBar`), dialogs, drag & drop and window buttons. Rust contains no engine logic.
- **Engine:** a C++ static library linked into the Tauri binary and reached through a small C ABI (`gmdr_create`, `gmdr_call(json) → json`, `gmdr_free`, `gmdr_set_event_sink`). All capabilities are named commands in one table shared by the UI, `gmdr-cli` and a future scripting API.
- **Import:** `.dem` parsing runs in `gmdr-import.exe`, spawned by the engine with two inherited handles (read `.dem`, write `state.gmstate.part`) inside a Job Object (memory cap, no child processes, kill-on-close). The child never opens a path. It reports progress and ready chunks as JSON lines on stdout. The main process reads only the resulting state file and validates every offset and checksum.

## Alternatives Considered
| Alternative | Pros | Cons | Why Not |
|-------------|------|------|---------|
| C++ host + system WebView (WebView2 / WebKitGTK) | one language, smallest binary | frameless window, packaging, updater all hand-made | user chose Tauri |
| Electron + engine as a separate server | most mature web host, crash isolation | ~150 MB runtime, every query serialized across processes, viewport frames across processes | heavy, slow queries |
| Everything in one process | simplest | a malformed demo can crash or compromise the app | violates the trust boundary from "Слабкі місця" |
| Whole engine as a sidecar server | max isolation, CLI = same binary | IPC on every scrub query, cross-process viewport | latency and complexity for little gain over isolating only the parser |
| Native UI (Dear ImGui/Qt) with Anvil tokens | no web | re-implement 100+ Anvil components | loses the design system |
| Parsers in Rust (now cheap, Rust is in the build) | memory safety by construction | splits the core across two languages; parsers must sit next to C++ render later | kept C++ + isolation + fuzzing |

## Consequences

### Positive
- Scrub queries are in-process function calls (microseconds); no serialization of engine state.
- Parser bugs are contained in a restricted child; the main process only reads a validated file.
- The command table is the automation API from day one; the CLI proves the UI is not special.
- A later viewport can get the native window handle from Tauri and render in-process.

### Negative
- Two toolchains (MSVC/CMake + cargo) and an FFI seam to maintain.
- Import needs a progress protocol and a file format that can be read while it is being written.

### Risks
- MSVC runtime mismatch between Rust and C++ → both use the dynamic CRT (`/MD`); build.rs pins the CMake preset.
- Job Object limits too tight for 6-hour demos → cap is a setting (default 4 GB), measured on corpus.
- AppContainer not in place at first → tracked as the last task of the sub-project; the handle-only child already denies path access by design.

## Dependencies
- ADR-002 (state file format), ADR-003 (command bus), ADR-004 (Anvil vendoring).
- Constrains the viewport sub-project: it must render in the main process into a surface owned by the Tauri window.
