# DemoGmodRender

Настільний застосунок, що відкриває демо Garry's Mod (`.dem`) без запуску гри: розбирає їх власним C++-рушієм, показує сутності, події й камеру автора та перевіряє, чи є на цьому комп'ютері весь потрібний вміст. Рендер відео — наступний підпроєкт.

Інтерфейс — Tauri 2 з дизайн-системою Anvil; рушій — C++20 за C ABI з однією JSON-командною шиною. Розбір чужих демо відбувається в окремому процесі `gmdr-import` без доступу до файлів, крім переданих дескрипторів.

## Структура

| Тека | Що |
| --- | --- |
| `engine/` | C++ рушій: `core`, `demo` (парсер, база стану), `assets` (Steam/GMod, VPK, GMA, VFS), `api` (командна шина, `gmdr.h`), `tools/` (`gmdr-import`, `gmdr-cli`), `tests/`, `fuzz/` |
| `app/src-tauri/` | оболонка Tauri: збирає рушій, лінкує його, пакує `gmdr-import` |
| `app/ui/` | інтерфейс: React 18 + TypeScript + Anvil (`src/vendor/anvil`) |
| `.10x/` | специфікація, ADR, рішення й стан розробки |

## Вимоги

- Windows 10/11 x64, Visual Studio 2022+ з C++ і компонентом vcpkg (або `VCPKG_ROOT` на власну копію vcpkg)
- Node.js 22+, Rust (stable, `x86_64-pc-windows-msvc`)
- WebView2 Runtime (є в Windows 11)

## Збирання й запуск

```bat
scripts\build-engine.cmd windows-msvc-release
```

```bat
cd app && npm ci && npm run dev
```

`npm run dev` збирає рушій (через `build.rs`), запускає Vite і відкриває вікно. Реліз без інсталятора: `npm run build`.

## Тести

- Рушій: `scripts\build-engine.cmd windows-msvc-debug`, потім `ctest --preset windows-msvc-debug` у `engine/` (юніт-тести й фазинг-прогони).
- Інтерфейс: `npm run typecheck` і `npm test` у `app/`.
- Linux (як у CI): `docker build -t gmdr-engine-linux -f engine/docker/linux.Dockerfile engine/docker`, потім `docker run --rm -v "%cd%:/src:ro" gmdr-engine-linux linux-gcc` (або `linux-clang-asan 60` — із фазингом по 60 с).
- На власних демо: `gmdr-cli verify <demo.dem>` порівнює базу стану зі станом парсера; `gmdr-cli run <demo.dem> demo.info` проганяє команди шини.
- Вікно застосунку: `node app/ui/scripts/smoke-app.mjs` після `npm run build`.

## Приватність

Демо містять дані інших гравців: їх ніколи не додають у репозиторій (`*.dem` і `*.gmstate` в `.gitignore`). Кеш імпорту лежить у `%LOCALAPPDATA%\DemoGmodRender`, налаштування — у `%APPDATA%\DemoGmodRender`.
