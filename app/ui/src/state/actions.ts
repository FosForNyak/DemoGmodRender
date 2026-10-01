// Everything the UI asks the engine to do. Components call these; they update the store.
import { call, errorOf, onEngineEvent } from '../api/engine';
import { pickDemoFile, pickFolder } from '../api/platform';
import type {
  AppInfo,
  CacheInfo,
  ContentReport,
  DemoInfo,
  DemoSession,
  EngineErrorInfo,
  EngineEvent,
  GmodInfo,
  RecentDemo,
} from '../api/types';
import { uk } from '../i18n/uk';
import { formatBytes, formatFloat, formatInt } from '../util/format';
import { getState, setDemo, setState, setUi, type AppState, type LogEntry, type ToastEntry } from './store';

let logId = 1;
let toastId = 1;

export function log(level: LogEntry['level'], text: string, source?: string): void {
  const time = new Date().toTimeString().slice(0, 8);
  setState((s) => ({ log: [...s.log.slice(-1999), { id: logId++, time, level, source, text }] }));
}

export function toast(severity: ToastEntry['severity'], title: string, text?: string): void {
  const id = toastId++;
  setState((s) => ({ toasts: [...s.toasts, { id, severity, title, text }] }));
  if (severity === 'success' || severity === 'info') setTimeout(() => dismissToast(id), 5000);
}

export function dismissToast(id: number): void {
  setState((s) => ({ toasts: s.toasts.filter((t) => t.id !== id) }));
}

function logError(source: string, e: EngineErrorInfo): void {
  log('error', `${e.message}${e.details ? ` (${e.details})` : ''} [${e.code}]`, source);
}

// ---- startup ----------------------------------------------------------------------------------------------

// Subscribes to engine events and loads the initial state; returns the unsubscribe function (React StrictMode
// mounts effects twice in development, so the subscription must be undoable).
export function init(): () => void {
  const unsubscribe = onEngineEvent(handleEvent);
  void loadInitial();
  return unsubscribe;
}

async function loadInitial(): Promise<void> {
  try {
    const [appInfo, settings] = await Promise.all([
      call<AppInfo>('app.info'),
      call<{ values: Record<string, unknown> }>('settings.get'),
    ]);
    setState({
      appInfo,
      settings: settings.values,
      recent: (settings.values.recent as RecentDemo[] | undefined) ?? [],
    });
  } catch (e) {
    logError('app', errorOf(e));
  }
  void locateGmod(false);
  void refreshCache();
}

export async function locateGmod(refresh: boolean): Promise<void> {
  setState((s) => ({ gmod: { ...s.gmod, status: 'loading' } }));
  try {
    const info = await call<GmodInfo>('gmod.locate', { refresh });
    setState({ gmod: { status: 'found', info } });
    for (const w of info.warnings) log('warn', w, 'gmod');
  } catch (e) {
    const error = errorOf(e);
    setState({ gmod: { status: 'missing', error } });
    log('warn', error.message, 'gmod');
  }
}

export async function refreshCache(): Promise<void> {
  try {
    setState({ cache: await call<CacheInfo>('cache.info') });
  } catch (e) {
    logError('cache', errorOf(e));
  }
}

// ---- settings -----------------------------------------------------------------------------------------------

export async function setSetting(key: string, value: unknown): Promise<void> {
  setState((s) => {
    const settings = { ...s.settings };
    if (value === null || value === undefined) delete settings[key];
    else settings[key] = value;
    return { settings };
  });
  try {
    await call('settings.set', { key, value: value ?? null });
  } catch (e) {
    logError('settings', errorOf(e));
  }
  if (key === 'gmod.path') await locateGmod(true);
  if (key === 'cache.limitBytes') await refreshCache();
}

export async function browseGmodFolder(): Promise<void> {
  const path = await pickFolder(uk.settings.gmodBrowse);
  if (path) await setSetting('gmod.path', path);
}

export async function clearCache(): Promise<void> {
  setUi({ clearCacheDialog: false });
  try {
    const r = await call<{ freedBytes: number; removed: number }>('cache.clear');
    toast('success', uk.toasts.cacheCleared, uk.toasts.cacheClearedText(formatBytes(r.freedBytes)));
  } catch (e) {
    logError('cache', errorOf(e));
  }
  await refreshCache();
}

// ---- demos -------------------------------------------------------------------------------------------------

export async function openDialog(): Promise<void> {
  const path = await pickDemoFile(uk.menu.open, 'Garry’s Mod demo');
  if (path) await openDemo(path);
}

export async function openDemo(path: string): Promise<void> {
  if (getState().opening) return;
  setState({ opening: path, openError: undefined });
  try {
    const current = getState().demo;
    if (current && current.path !== path) await closeDemo();
    const session = await call<DemoSession>('demo.open', { path });
    setState((s) => ({
      opening: undefined,
      demo: {
        id: session.demo,
        name: session.name,
        path: session.path,
        imp: session.import,
        tick: s.demo?.id === session.demo ? s.demo.tick : 0,
        playing: false,
      },
      ui: { ...s.ui, bottomTab: 'timeline' },
    }));
    log('info', `${session.name}: ${session.import.cached ? 'з кешу' : 'імпорт'}`, 'demo');
    await refreshInfo();
    await refreshRecent();
    // A cached demo is ready at once: no import.done event will start the content check.
    if (session.import.state === 'ready') void checkContent();
  } catch (e) {
    const error = errorOf(e);
    setState({ opening: undefined, openError: error });
    logError('demo', error);
  }
}

export async function closeDemo(): Promise<void> {
  const demo = getState().demo;
  if (!demo) return;
  setState({ demo: undefined });
  try {
    await call('demo.close', { demo: demo.id });
  } catch (e) {
    logError('demo', errorOf(e));
  }
  void refreshCache();
}

export async function reopenDemo(): Promise<void> {
  const demo = getState().demo;
  if (!demo) return;
  const path = demo.path;
  await closeDemo();
  await openDemo(path);
}

async function refreshRecent(): Promise<void> {
  try {
    const r = await call<{ value: RecentDemo[] | null }>('settings.get', { key: 'recent' });
    setState({ recent: r.value ?? [] });
  } catch {
    /* recent list is optional */
  }
}

export async function refreshInfo(): Promise<void> {
  const demo = getState().demo;
  if (!demo) return;
  try {
    const info = await call<DemoInfo>('demo.info', { demo: demo.id });
    setDemo((d) => (d.id === info.demo ? { info, imp: info.import } : {}));
  } catch (e) {
    logError('demo', errorOf(e));
  }
}

export async function cancelImport(): Promise<void> {
  const demo = getState().demo;
  if (demo) await call('import.cancel', { demo: demo.id }).catch((e) => logError('import', errorOf(e)));
}

export async function checkContent(): Promise<void> {
  const demo = getState().demo;
  if (!demo || demo.imp.state !== 'ready') return;
  setDemo({ content: { status: 'loading' } });
  try {
    const report = await call<ContentReport>('content.check', { demo: demo.id });
    setDemo((d) => (d.id === demo.id ? { content: { status: 'ready', report } } : {}));
  } catch (e) {
    const error = errorOf(e);
    setDemo((d) => (d.id === demo.id ? { content: { status: 'error', error } } : {}));
    logError('content', error);
  }
}

// ---- playback ----------------------------------------------------------------------------------------------

export function tickRate(s: AppState = getState()): number {
  return s.demo?.info?.tickRate ?? 66;
}

export function maxTick(s: AppState = getState()): number {
  const d = s.demo;
  if (!d) return 0;
  return d.imp.state === 'ready' ? Math.max(d.imp.lastTick, d.imp.readyTick) : Math.max(0, d.imp.readyTick);
}

export function seek(tick: number): void {
  const d = getState().demo;
  if (!d) return;
  const hi = Math.max(0, d.imp.state === 'ready' ? d.imp.lastTick : d.imp.readyTick);
  setDemo({ tick: Math.round(Math.min(hi, Math.max(0, tick))) });
}

export function step(delta: number): void {
  const d = getState().demo;
  if (d) seek(d.tick + delta);
}

export function setPlaying(playing: boolean): void {
  setDemo({ playing });
}

export function select(uid: string | undefined): void {
  setDemo({ selected: uid });
}

// ---- engine events -----------------------------------------------------------------------------------------

function handleEvent(e: EngineEvent): void {
  const demo = getState().demo;
  switch (e.type) {
    case 'import.indexed':
      if (demo?.id === e.demo)
        setDemo((d) => ({ imp: { ...d.imp, state: 'importing', firstTick: e.firstTick, lastTick: e.lastTick } }));
      break;
    case 'import.progress':
      if (demo?.id === e.demo) {
        const first = demo.imp.readyTick < 0 && e.readyTick >= 0;
        setDemo((d) => ({ imp: { ...d.imp, readyTick: e.readyTick, lastTick: e.lastTick } }));
        if (first) void refreshInfo();
      }
      break;
    case 'import.done':
      if (demo?.id === e.demo) {
        void refreshInfo().then(() => {
          void checkContent();
          void refreshCache();
        });
        const seconds = e.stats.seconds !== undefined ? formatFloat(e.stats.seconds, 1) : '';
        toast('success', uk.toasts.importDone, uk.toasts.importDoneText(demo.name, seconds));
        log('info', `імпорт завершено: ${formatInt(e.stats.packets ?? 0)} пакетів за ${seconds} с`, 'import');
      }
      break;
    case 'import.failed':
      if (demo?.id === e.demo) {
        setDemo((d) => ({
          imp: {
            ...d.imp,
            state: e.error.code === 'import.cancelled' ? 'cancelled' : 'failed',
            error: e.error,
            readyTick: e.readyTick ?? d.imp.readyTick,
          },
        }));
        logError('import', e.error);
        if (e.error.code !== 'import.cancelled') toast('error', uk.toasts.importFailed, e.error.message);
      }
      break;
    case 'settings.changed':
      break;
    case 'log':
      log(e.level === 'error' ? 'error' : e.level === 'warn' ? 'warn' : 'info', e.message, 'engine');
      break;
  }
}
