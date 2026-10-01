# Handoff

**From:** 10x team (phases 0–6 of app-shell-demo-import)
**To:** the owner (review PR #1), then the next sub-project (rendering)

- Read first: `status.md`, `reviews/qa-app-shell-demo-import.md`, `reviews/security-app-shell-demo-import.md`.
- Decisions waiting: AppContainer for `gmdr-import` (security S1); merge of PR #1 into `beta`.
- Build/run: `README.md`; CI: `decisions/devops/app-shell-demo-import.md`; runtime behaviour: `decisions/sre/app-shell-demo-import.md`.
- Local corpus checks: `GMDR_CORPUS=<folder with .dem>` for `gmdr_tests`; `gmdr-cli verify|gate <demo>`.
