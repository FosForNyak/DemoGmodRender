// One small external store for the whole UI (read through useApp(selector)); actions live in actions.ts.
import { useSyncExternalStore } from 'react';
import type {
  AppInfo,
  CacheInfo,
  ContentReport,
  DemoInfo,
  EngineErrorInfo,
  GmodInfo,
  ImportStatus,
  RecentDemo,
} from '../api/types';

export interface LogEntry {
  id: number;
  time: string;
  level: 'error' | 'warn' | 'info' | 'debug';
  source?: string;
  text: string;
}

export interface ToastEntry {
  id: number;
  severity: 'info' | 'success' | 'warning' | 'error';
  title: string;
  text?: string;
}

export interface DemoState {
  id: string;
  name: string;
  path: string;
  imp: ImportStatus;
  info?: DemoInfo;
  tick: number;
  playing: boolean;
  selected?: string; // entity life uid
  content?: { status: 'idle' | 'loading' | 'ready' | 'error'; report?: ContentReport; error?: EngineErrorInfo };
}

export type Theme = 'dark' | 'light' | 'system';
export type DensitySetting = 'compact' | 'comfortable' | 'spacious';

export interface AppState {
  appInfo?: AppInfo;
  gmod: { status: 'unknown' | 'loading' | 'found' | 'missing'; info?: GmodInfo; error?: EngineErrorInfo };
  settings: Record<string, unknown>;
  recent: RecentDemo[];
  cache?: CacheInfo;
  demo?: DemoState;
  opening?: string; // path being opened
  openError?: EngineErrorInfo;
  log: LogEntry[];
  toasts: ToastEntry[];
  ui: {
    settingsOpen: boolean;
    settingsSection?: string;
    paletteOpen: boolean;
    bottomTab: 'timeline' | 'content' | 'log';
    centerTab: 'top' | '3d';
    focusedPanel: 'outliner' | 'center' | 'inspector' | 'bottom';
    dragOver: boolean;
    maximized: boolean;
    fullscreen: boolean;
    gmodBannerClosed: boolean;
    clearCacheDialog: boolean;
  };
}

const initial: AppState = {
  gmod: { status: 'unknown' },
  settings: {},
  recent: [],
  log: [],
  toasts: [],
  ui: {
    settingsOpen: false,
    paletteOpen: false,
    bottomTab: 'timeline',
    centerTab: 'top',
    focusedPanel: 'center',
    dragOver: false,
    maximized: false,
    fullscreen: false,
    gmodBannerClosed: false,
    clearCacheDialog: false,
  },
};

let state = initial;
const listeners = new Set<() => void>();

export function getState(): AppState {
  return state;
}

export function setState(update: Partial<AppState> | ((s: AppState) => Partial<AppState>)): void {
  const patch = typeof update === 'function' ? update(state) : update;
  state = { ...state, ...patch };
  for (const l of listeners) l();
}

export function setUi(patch: Partial<AppState['ui']>): void {
  setState((s) => ({ ui: { ...s.ui, ...patch } }));
}

export function setDemo(patch: Partial<DemoState> | ((d: DemoState) => Partial<DemoState>)): void {
  setState((s) => {
    if (!s.demo) return {};
    const p = typeof patch === 'function' ? patch(s.demo) : patch;
    return { demo: { ...s.demo, ...p } };
  });
}

function subscribe(l: () => void): () => void {
  listeners.add(l);
  return () => listeners.delete(l);
}

// Selectors must return stored values or primitives (not new objects) to keep renders cheap.
export function useApp<T>(selector: (s: AppState) => T): T {
  return useSyncExternalStore(subscribe, () => selector(state));
}
