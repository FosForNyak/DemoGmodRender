# Staff Engineer — app-shell-demo-import

**Date:** 2026-09-30

## Patterns
- **Push parser.** `DemoParser` walks commands and calls a `DemoSink` interface (`onHeader`, `onServerInfo`, `onStringTable*`, `onSchema`, `onEntityEnter/Update/Leave/Delete`, `onEvent`, `onCamera`, `onTickEnd`). `StateWriter` and test collectors are sinks. Keeps decoding independent from storage.
- **ProtocolVariant** struct holds every width/layout that differs between builds; the parser reads it, never hard-coded constants.
- **BitReader** returns values and sets a sticky overflow flag instead of throwing per bit; callers check `ok()` at message boundaries (fast path, no exceptions in the hot loop). Message-level errors become `Result` errors.
- **Command registry:** `registry.add("demo.info", schema, handler)`; handlers are free functions taking `(Engine&, const json&) → Result<json>`.
- **UI state:** one store (React context + reducer) per open demo; engine calls go through a typed `engine.ts` wrapper; no business logic in components.

## Cross-cutting concerns
| Concern | Decision |
| --- | --- |
| Threading | engine calls may come from any Tauri worker thread; each demo session guarded by a mutex; importer supervision on a dedicated thread; event sink callback must be non-blocking (Rust side queues) |
| Memory | LRU budget 512 MB for decoded chunks; importer capped by Job Object (4 GB) |
| Paths | UTF-8 everywhere in the engine; `std::filesystem::path` from UTF-8 via `u8path` equivalent; Windows APIs via wide strings in `core/platform` only |
| Time | `Tick` i64; `Flicks` i64 for timeline; conversions in `core/time.h` |
| Errors to UI | `{code, message, details}`; UI maps codes to InfoBar/EmptyState/HelperText per spec §10 |
| i18n | UI strings in `ui/src/i18n/uk.ts`; engine returns codes, not prose, where UI shows them |

## Reuse
Nothing to reuse in the repo (fresh start). External: vcpkg `zstd`, `blake3`, `xxhash`, `nlohmann-json`, `doctest`; Tauri 2 plugins `dialog`, `window-state` (optional); npm `react@18`, `react-dom@18`, `vite`, `typescript`, `vitest`.
