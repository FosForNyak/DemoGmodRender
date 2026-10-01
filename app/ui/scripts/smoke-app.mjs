// Smoke test of the real app window: starts the built exe with WebView2 remote debugging on localhost,
// checks that the UI rendered and that the engine answers through Tauri IPC, saves a screenshot, closes the app.
// Usage: node ui/scripts/smoke-app.mjs [path/to/demogmodrender.exe] [screenshot.png]
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const exe = process.argv[2] ?? join(here, '..', '..', 'src-tauri', 'target', 'release', 'demogmodrender.exe');
const shot = process.argv[3] ?? join(process.env.TEMP ?? '.', 'gmdr-smoke.png');
const port = 9229;

// Own cache and settings, so the run leaves the user's recent list and cache alone; a tiny synthetic demo
// (header + dem_stop) is imported through the real app to check the importer sandbox.
const scratch = mkdtempSync(join(tmpdir(), 'gmdr-smoke-'));
const demoPath = join(scratch, 'smoke.dem');
const demo = Buffer.alloc(1072 + 5);
demo.write('HL2DEMO', 0, 'ascii');
demo.writeInt32LE(3, 8);
demo.writeInt32LE(24, 12);
demo.write('gm_smoke', 16 + 520, 'ascii');
demo[1072] = 7; // dem_stop
writeFileSync(demoPath, demo);

const app = spawn(exe, [], {
  env: {
    ...process.env,
    WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: `--remote-debugging-port=${port}`,
    GMDR_CACHE_DIR: join(scratch, 'cache'),
    GMDR_CONFIG_DIR: join(scratch, 'config'),
  },
  stdio: 'inherit',
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failed = false;
const check = (name, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? `: ${detail}` : ''}`);
  if (!ok) failed = true;
};

try {
  let target;
  for (let i = 0; i < 60 && !target; i++) {
    await sleep(500);
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
      target = list.find((t) => t.type === 'page');
    } catch {
      /* not up yet */
    }
  }
  check('webview page is up', !!target, target?.url);
  if (!target) throw new Error('no page');

  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r, j) => ((ws.onopen = r), (ws.onerror = j)));
  let id = 0;
  const pending = new Map();
  ws.onmessage = (m) => {
    const msg = JSON.parse(m.data);
    if (pending.has(msg.id)) pending.get(msg.id)(msg);
  };
  const send = (method, params = {}) =>
    new Promise((r) => {
      const n = ++id;
      pending.set(n, r);
      ws.send(JSON.stringify({ id: n, method, params }));
    });
  const evaluate = async (expression) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
    if (r.result?.exceptionDetails) throw new Error(r.result.exceptionDetails.text);
    return r.result?.result?.value;
  };

  for (let i = 0; i < 40; i++) {
    if (await evaluate(`!!document.querySelector('.av-ws')`)) break;
    await sleep(250);
  }
  check('Anvil workspace rendered', await evaluate(`!!document.querySelector('.av-ws')`));
  check('title bar', await evaluate(`document.querySelector('.av-ws')?.textContent.includes('DemoGmodRender')`));
  check('fonts loaded', await evaluate(`document.fonts.ready.then(() => document.fonts.check('13px "IBM Plex Sans"'))`));
  const info = await evaluate(
    `window.__TAURI_INTERNALS__.invoke('engine_call', { request: JSON.stringify({ cmd: 'app.info' }) }).then(JSON.parse)`,
  );
  check('engine over IPC', info?.ok === true, info?.ok ? `v${info.result.version}, importer found: ${info.result.importerFound}` : JSON.stringify(info));
  const gmod = await evaluate(
    `window.__TAURI_INTERNALS__.invoke('engine_call', { request: JSON.stringify({ cmd: 'gmod.locate' }) }).then(JSON.parse)`,
  );
  check('gmod.locate', gmod?.ok === true, gmod?.ok ? `${gmod.result.gmas} GMA` : gmod?.error?.code);
  const csp = await evaluate(`fetch('https://example.com').then(() => 'allowed', () => 'blocked')`);
  check('CSP blocks remote fetch', csp === 'blocked', csp);

  const engineCall = (cmd, args) =>
    evaluate(
      `window.__TAURI_INTERNALS__.invoke('engine_call', { request: ${JSON.stringify(JSON.stringify({ cmd, args }))} }).then(JSON.parse)`,
    );
  const opened = await engineCall('demo.open', { path: demoPath });
  check('demo.open (synthetic demo)', opened?.ok === true, opened?.ok ? opened.result.demo : JSON.stringify(opened?.error));
  if (opened?.ok) {
    let info;
    for (let i = 0; i < 40; i++) {
      info = await engineCall('demo.info', { demo: opened.result.demo });
      if (info?.ok && info.result.import.state !== 'indexing' && info.result.import.state !== 'importing') break;
      await sleep(250);
    }
    check('import finished', info?.result?.import?.state === 'ready', info?.result?.import?.state);
    check('importer ran in its AppContainer', info?.result?.import?.sandbox === 'appcontainer', info?.result?.import?.sandbox);
  }

  await sleep(800);
  const png = await send('Page.captureScreenshot', { format: 'png' });
  if (png.result?.data) {
    writeFileSync(shot, Buffer.from(png.result.data, 'base64'));
    console.log(`screenshot: ${shot}`);
  }
  ws.close();
} catch (e) {
  check('smoke test', false, String(e));
} finally {
  app.kill();
  await sleep(500);
  rmSync(scratch, { recursive: true, force: true });
}
process.exit(failed ? 1 : 0);
