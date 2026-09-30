// The UI's only way to the engine: the JSON command bus (ADR-003). In the app it goes through the Tauri
// command `engine_call` and the event "engine"; in a plain browser tab (npm run dev:browser) through the dev
// bridge (ui/scripts/dev-bridge.mjs) that runs the same engine.
import type { EngineErrorInfo, EngineEvent } from './types';

export class EngineError extends Error {
  readonly code: string;
  readonly details?: string;
  constructor(info: EngineErrorInfo) {
    super(info.message);
    this.code = info.code;
    this.details = info.details;
  }
}

export const inTauri = typeof window !== 'undefined' && '__TAURI_INTERNALS__' in window;

type Response = { ok: true; result: unknown } | { ok: false; error: EngineErrorInfo };

async function send(request: string): Promise<Response> {
  if (inTauri) {
    const { invoke } = await import('@tauri-apps/api/core');
    return JSON.parse(await invoke<string>('engine_call', { request })) as Response;
  }
  const res = await fetch('/bridge/call', { method: 'POST', body: request });
  if (!res.ok)
    return { ok: false, error: { code: 'ui.bridge', message: `dev bridge returned ${res.status}` } };
  return (await res.json()) as Response;
}

export async function call<T>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  const response = await send(JSON.stringify({ cmd, args }));
  if (!response.ok) throw new EngineError(response.error);
  return response.result as T;
}

// Subscribes to engine events; returns the unsubscribe function.
export function onEngineEvent(handler: (e: EngineEvent) => void): () => void {
  if (inTauri) {
    let unlisten: (() => void) | undefined;
    let cancelled = false;
    import('@tauri-apps/api/event').then(({ listen }) =>
      listen<string>('engine', (e) => handler(JSON.parse(e.payload) as EngineEvent)).then((u) => {
        if (cancelled) u();
        else unlisten = u;
      }),
    );
    return () => {
      cancelled = true;
      unlisten?.();
    };
  }
  const source = new EventSource('/bridge/events');
  source.onmessage = (e) => handler(JSON.parse(e.data) as EngineEvent);
  return () => source.close();
}

export function errorOf(e: unknown): EngineErrorInfo {
  if (e instanceof EngineError) return { code: e.code, message: e.message, details: e.details };
  return { code: 'ui.unexpected', message: e instanceof Error ? e.message : String(e) };
}
