# CTO — index

Cross-cutting direction for DemoGmodRender (engine that renders GMod demos to video without running GMod).

- Engine is its own renderer and parser; GMod is never launched or injected (architecture doc, 2026-09-30).
- Stack: C++20 engine (core → demo → assets → scene → sim → render → output), Tauri 2 (Rust) desktop shell, React 18 + Anvil design system UI.
- Delivery is split into sub-projects that follow the 7-phase roadmap; each sub-project ends with a measurable gate.
- Untrusted input (demos, Workshop content) is parsed only in an isolated child process.

## Features
| Slug | File | Status |
| --- | --- | --- |
| app-shell-demo-import | [app-shell-demo-import.md](app-shell-demo-import.md) | in progress |
