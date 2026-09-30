import react from '@vitejs/plugin-react';
import { defineConfig } from 'vitest/config';

export default defineConfig({
  root: 'ui',
  plugins: [react()],
  clearScreen: false,
  server: {
    port: 5173,
    strictPort: true,
    // Browser development only (npm run dev:browser): the engine through ui/scripts/dev-bridge.mjs.
    proxy: {
      '/bridge': { target: 'http://127.0.0.1:5174', rewrite: (p) => p.replace(/^\/bridge/, '') },
    },
  },
  build: { outDir: 'dist', emptyOutDir: true, target: 'es2022' },
  test: { root: '.', include: ['ui/src/**/*.test.ts'] },
});
