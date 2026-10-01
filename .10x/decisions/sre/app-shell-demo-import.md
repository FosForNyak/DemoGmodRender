# SRE — app-shell-demo-import (desktop runtime)

## Failure modes and what the app does
| Failure | Detection | Result |
| --- | --- | --- |
| Importer crash or kill (bug, memory cap 4 GB) | pipe closes without `done` | `import.failed` with `import.crashed` + exit code; the part already imported stays readable; the banner offers "Повторити" |
| Importer hang | no line for 60 s (watchdog) | killed; `import.stalled` |
| User cancels / closes the demo | `import.cancel` / `demo.close` | Job Object terminated; on close the `.part` file is removed |
| Damaged or foreign cache file | header/version/hash or any chunk check fails | `statedb.*` error; on open the state is rebuilt by a new import |
| Cache grows | after each close | LRU by last use down to `cache.limitBytes` (default 20 GB); open demos are never removed; "Очистити кеш…" in settings |
| GMod not found / moved | `gmod.locate` | banner + settings path field; content check disabled until found |
| Corrupt settings.json | parse error at start | defaults, warning in the log; only valid keys are kept on the next save (atomic write) |

## Resources
Main process: decoded chunk LRU ≤ 512 MB, 4 keyframe states and 8 composed string-table states cached per open demo. Importer: Job Object 4 GB; single process.

## Observability
Engine events (`log`, `import.*`) feed the UI log panel; no telemetry, no files outside `%LOCALAPPDATA%\DemoGmodRender` (cache) and `%APPDATA%\DemoGmodRender` (settings). `gmdr-cli` reproduces any engine command for support.
