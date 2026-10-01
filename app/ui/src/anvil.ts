// Typed access to the vendored Anvil design system. vendor/anvil/bundle.js assigns window.Anvil; its type
// (and the global Window declaration) comes from vendor/anvil/index.d.ts.
import type * as AnvilTypes from './vendor/anvil/index';

export type { AnvilTypes };

export const Anvil = window.Anvil;
