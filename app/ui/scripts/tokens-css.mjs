// Compiles the vendored Anvil tokens.json into tokens.css (ADR-004):
//   :root, [data-theme="dark"]  colours and shadows of the first theme (dark)
//   [data-theme="light"]        light overrides
//   :root                       spacing, radius, size, breakpoint, duration, easing, layer, --font-*
//   .t-<style>                  one class per text style
//   @font-face                  one per font file
// Aliases "{token}" become var(--token). Run: node scripts/tokens-css.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const dir = join(here, '..', 'src', 'vendor', 'anvil');
const tokens = JSON.parse(readFileSync(join(dir, 'tokens.json'), 'utf8'));

const value = (v) => String(v).replace(/\{([a-z0-9-]+)\}/g, 'var(--$1)');
const themes = tokens.color.themes.map((t) => t.id);
const [baseTheme] = themes;

const out = ['/* Generated from tokens.json by scripts/tokens-css.mjs. Do not edit. */', ''];

for (const font of tokens.type.fonts) {
  out.push(
    `@font-face { font-family: "${font.family}"; src: url("./${font.file}") format("woff2"); ` +
      `font-weight: ${font.weight}; font-style: ${font.style}; font-display: swap; }`,
  );
}
out.push('');

const themed = [...tokens.color.tokens, ...tokens.shadow.tokens];
for (const theme of themes) {
  const selector = theme === baseTheme ? `:root, [data-theme="${theme}"]` : `[data-theme="${theme}"]`;
  out.push(`${selector} {`);
  if (theme === baseTheme) out.push(`  color-scheme: ${theme};`);
  else out.push(`  color-scheme: ${theme};`);
  for (const t of themed) {
    const v = typeof t.value === 'object' ? t.value[theme] : theme === baseTheme ? t.value : undefined;
    if (v !== undefined) out.push(`  --${t.name}: ${value(v)};`);
  }
  out.push('}', '');
}

out.push(':root {');
for (const family of ['spacing', 'radius', 'size', 'breakpoint', 'duration', 'easing', 'layer']) {
  for (const t of tokens[family].tokens) out.push(`  --${t.name}: ${value(t.value)};`);
}
for (const [name, stack] of Object.entries(tokens.type.families)) out.push(`  --font-${name}: ${stack};`);
out.push('}', '');

for (const group of tokens.type.groups) {
  for (const s of group.styles) {
    const decl = [
      `font-family: var(--font-${s.family ?? group.family})`,
      `font-size: ${s.fontSize}`,
      `line-height: ${s.lineHeight}`,
      `font-weight: ${s.fontWeight ?? 400}`,
    ];
    if (s.letterSpacing) decl.push(`letter-spacing: ${s.letterSpacing}`);
    if (s.name === 'overline') decl.push('text-transform: uppercase');
    if ((s.family ?? group.family) === 'mono') decl.push('font-variant-numeric: tabular-nums');
    out.push(`.t-${s.name} { ${decl.join('; ')}; }`);
  }
}
out.push('');

writeFileSync(join(dir, 'tokens.css'), out.join('\n'));
console.log(`tokens.css: ${themed.length} themed tokens x ${themes.length} themes, ${tokens.type.fonts.length} fonts`);
