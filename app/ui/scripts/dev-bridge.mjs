// Dev only: runs the real engine (gmdr-cli serve) and exposes the command bus to a browser tab, so the UI can
// be developed and checked outside the Tauri window. Listens on 127.0.0.1 only; never part of the app build.
//   POST /call    body {"cmd", "args"}  ->  engine response
//   GET  /events  server-sent events with engine events
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { createInterface } from 'node:readline';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const cli = join(here, '..', '..', '..', 'engine', 'build', 'windows-msvc-release', 'tools', 'gmdr-cli', 'gmdr-cli.exe');
const port = Number(process.env.GMDR_BRIDGE_PORT ?? 5174);

const engine = spawn(cli, ['serve'], { stdio: ['pipe', 'pipe', 'inherit'] });
engine.on('exit', (code) => {
  console.error(`gmdr-cli serve exited with ${code}`);
  process.exit(1);
});

let nextId = 1;
const pending = new Map();
const listeners = new Set();

createInterface({ input: engine.stdout }).on('line', (line) => {
  let msg;
  try {
    msg = JSON.parse(line);
  } catch {
    return;
  }
  if (msg.event) {
    const data = `data: ${JSON.stringify(msg.event)}\n\n`;
    for (const res of listeners) res.write(data);
  } else if (pending.has(msg.id)) {
    pending.get(msg.id)(JSON.stringify(msg.response));
    pending.delete(msg.id);
  }
});

createServer((req, res) => {
  if (req.method === 'POST' && req.url === '/call') {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', () => {
      let request;
      try {
        request = JSON.parse(body);
      } catch {
        res.writeHead(400).end();
        return;
      }
      const id = nextId++;
      pending.set(id, (text) => res.writeHead(200, { 'content-type': 'application/json' }).end(text));
      engine.stdin.write(JSON.stringify({ id, request }) + '\n');
    });
  } else if (req.method === 'GET' && req.url === '/events') {
    res.writeHead(200, { 'content-type': 'text/event-stream', 'cache-control': 'no-cache', connection: 'keep-alive' });
    res.write(': connected\n\n');
    listeners.add(res);
    req.on('close', () => listeners.delete(res));
  } else {
    res.writeHead(404).end();
  }
}).listen(port, '127.0.0.1', () => console.log(`engine bridge on http://127.0.0.1:${port}`));
