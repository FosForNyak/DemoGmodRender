# ADR-004: Vendor the Anvil bundle and generate `tokens.css` at build time

**Status:** Accepted
**Date:** 2026-09-30
**Feature:** app-shell-demo-import
**Author:** 10x-Team (Architect + Staff Engineer)

## Context
Anvil is published as a design-system artifact: `components/bundle.js` (a classic script that assigns `window.Anvil` and reads `window.React`/`ReactDOM` 18), `bundle.css`, `index.d.ts`, IBM Plex fonts, and `tokens.json`. The compiled `tokens.css` is produced by the artifact page and is not a published file. The app must work offline with a strict CSP (no CDNs).

## Decision
- Copy the published files into `app/ui/src/vendor/anvil/` (bundle.js, bundle.css, index.d.ts, tokens.json, fonts, licenses) with a `VERSION` note (artifact version id, date).
- `app/ui/scripts/tokens-css.mjs` compiles `tokens.json` into `tokens.css` following the documented grammar: `:root, [data-theme="dark"]` colors and shadows, `[data-theme="light"]` overrides, `:root` spacing/radius/other families and `--font-*`, a class per text style, `@font-face` per font file. Aliases `{token}` become `var(--token)`.
- The app sets `window.React`/`window.ReactDOM` from npm React 18 before importing the bundle as a side effect; types come from `index.d.ts` through a `declare global { interface Window { Anvil: typeof import('./vendor/anvil/index') } }`.
- Updating Anvil = re-copy files + bump `VERSION`; no runtime fetching.

## Alternatives Considered
| Alternative | Pros | Cons | Why Not |
|-------------|------|------|---------|
| Load the bundle from the artifact URL at runtime | always latest | needs network + auth, breaks CSP and offline use | not viable for a desktop app |
| Re-implement Anvil components in the app | full control | duplicates 100+ components, drifts from the system | defeats the design system |
| Hand-write tokens.css once | quick | drifts from tokens.json on every update | generator is small |

## Consequences

### Positive
- Offline, CSP `default-src 'self'`, reproducible builds.
- The design system stays the single source of look and behavior.

### Negative
- Vendored 350 KB JS + 180 KB CSS; updates are a manual copy.

### Risks
- The bundle expects React 18 exactly → pin `react@18`/`react-dom@18` in package.json.
- tokens grammar misread → a visual check of both themes in the app is part of QA.

## Dependencies
- ADR-001 (web UI in Tauri).
