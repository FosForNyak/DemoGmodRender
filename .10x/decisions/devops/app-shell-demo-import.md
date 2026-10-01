# DevOps — app-shell-demo-import

## Build
- Engine: `scripts/build-engine.cmd <preset> [targets]` — one Visual Studio instance (full edition over Build Tools) for the compiler and its vcpkg; always configures explicitly (a regeneration started by ninja can leave the build folder half-written). Presets: `windows-msvc-debug`, `windows-msvc-release`, `linux-gcc`, `linux-clang-asan`.
- App: `app/` — `npm ci`, `npm run dev` (Vite + Tauri, builds the engine through `build.rs`), `npm run build` (release exe without installer). `GMDR_SKIP_ENGINE_BUILD=1` skips the engine step when it is already built.
- Linux toolchain locally: `engine/docker/linux.Dockerfile` (Ubuntu 24.04, GCC 13, Clang 18 + compiler-rt, vcpkg); `run-preset <preset> [fuzz-seconds]`.

## CI (`.github/workflows/ci.yml`)
| Job | Runner | What |
| --- | --- | --- |
| Engine · Windows MSVC | windows-2025 | Debug build, ctest (unit + fuzz smoke) |
| Engine · Linux linux-gcc | ubuntu-24.04 | build, ctest |
| Engine · Linux linux-clang-asan | ubuntu-24.04 | ASan/UBSan build, ctest, 60 s libFuzzer per target; crash inputs uploaded as `fuzz-findings` |
| App · Windows (Tauri) | windows-2025, after the engine job | `npm ci`, tokens, typecheck, vitest, `tauri build --bundles nsis`; installer uploaded as an artifact for 14 days |

vcpkg binary caches per OS/preset keyed by `vcpkg.json` + `vcpkg-configuration.json`; Rust cache for `app/src-tauri`; npm cache.

## Release
Nothing is published from CI. A release means: merge `beta` → `release` by the owner, tag, and attach the CI installer artifact — each step done by the owner. No file associations are registered by the installer.
