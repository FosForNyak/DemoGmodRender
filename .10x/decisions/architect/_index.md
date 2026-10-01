# Architect — index

System-wide structure (see ADR-001…004):
- Processes: `DemoGmodRender.exe` (Tauri + engine lib), `gmdr-import.exe` (untrusted parsing), `gmdr-cli.exe` (headless client).
- Engine modules, dependencies only downward: `core` → `demo` → `assets` → `api`. Future: `scene`, `sim`, `render`, `output`.
- Trust boundary: everything derived from `.dem` is parsed in the child; the main process reads only `state.gmstate`, validating it.
- Identity and time types: `EntityUid` u64, `Tick` i64, `Flicks` i64; x64 only.

## Features
| Slug | File | ADRs |
| --- | --- | --- |
| app-shell-demo-import | [app-shell-demo-import.md](app-shell-demo-import.md) | 001, 002, 003, 004 |
