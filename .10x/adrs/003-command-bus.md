# ADR-003: One command table as the engine API (UI, CLI, scripting)

**Status:** Accepted
**Date:** 2026-09-30
**Feature:** app-shell-demo-import
**Author:** 10x-Team (Architect + Staff Engineer)

## Context
The architecture doc: "будь-який інтерфейс і CLI — лише клієнти тих самих команд" and a bpy-like scripting API later. The FFI seam between Rust and C++ should stay small and stable while the engine grows.

## Decision
- The engine exposes a registry of named commands (`demo.open`, `state.entity`, `timeline.events`, …). Each command takes a JSON object and returns a JSON object or an error `{code, message, details?}` with stable dotted codes.
- The C ABI has four functions: `gmdr_create`, `gmdr_destroy`, `gmdr_call(engine, utf8 json request) → owned utf8 json response` (+ `gmdr_free`), `gmdr_set_event_sink(engine, callback, user)`.
- Events (import progress, logs) are JSON objects pushed through the sink; Rust re-emits them as the Tauri event `engine`.
- `gmdr-cli` is a thin client of the same registry (`gmdr call demo.info '{"path":"x.dem"}'` plus friendly aliases).
- A command's name and argument shape are its contract; changes are additive; removing or renaming needs a new name.

## Alternatives Considered
| Alternative | Pros | Cons | Why Not |
|-------------|------|------|---------|
| One C function per capability | typed, no JSON cost | FFI surface grows with every feature; Rust must mirror every signature | seam churn |
| cxx bridge (typed Rust↔C++) | type-safe | couples Rust build to C++ headers, bigger seam | same churn, heavier build |
| Binary protocol (flatbuffers/protobuf) | faster, typed | schema toolchain in three languages | premature; JSON cost is negligible at UI query rates |

## Consequences

### Positive
- Adding a capability touches only C++ (+ UI); Rust never changes.
- CLI and scripting come for free; commands are testable without the UI.

### Negative
- JSON encode/decode per call (~tens of µs); large payloads (positions of 1000 entities) must stay compact.

### Risks
- Hot paths (top-down view at 60 Hz) outgrow JSON → add a binary side channel for that one query later without changing the rest.

## Dependencies
- ADR-001.
