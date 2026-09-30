# Handoff

**From:** SDE (Phase 4, T1–T13 done)
**To:** SDE (Phase 4, T14 onward)

Read: spec `.10x/specs/2026-09-30-app-shell-demo-import-design.md` (§4 format, §5 import, §7 content), ADR-001…005, tasks in `decisions/engineering-manager/app-shell-demo-import.md`, log in `decisions/sde/app-shell-demo-import.md`.

Build: VS 18 `vcvars64` + `VCPKG_ROOT` = VS-bundled vcpkg, then `cmake --preset windows-msvc-release` / `cmake --build --preset windows-msvc-release`. Checks on local demos (never committed): `gmdr-cli verify <demo>`, `gmdr-cli spawn-import <demo> <out>`.

Next: T14–T15 `engine/assets` (VDF, Steam/GMod locator, VPK, GMA, ZIP, VFS order, content check against the MANIFEST chunk).
