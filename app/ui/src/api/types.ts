// Result shapes of the engine commands (engine/api/engine.cpp, API v1).

export interface EngineErrorInfo {
  code: string;
  message: string;
  details?: string;
}

export type ImportStateName = 'indexing' | 'importing' | 'ready' | 'failed' | 'cancelled';

export interface ImportStatus {
  state: ImportStateName;
  cached: boolean;
  firstTick: number;
  lastTick: number;
  readyTick: number;
  error?: EngineErrorInfo;
  stats?: Record<string, number>;
}

export interface DemoSession {
  demo: string;
  path: string;
  name: string;
  hash: string;
  import: ImportStatus;
}

export interface DemoInfo extends DemoSession {
  header?: { map: string; server: string; client: string; game: string; playbackTime: number; ticks: number };
  server?: { hostName: string; map: string; gamemode: string; tickInterval: number; maxClients: number; playerSlot: number };
  tickInterval?: number;
  tickRate?: number;
  durationSeconds?: number;
  decodeErrors?: string[];
}

export type EntityGroup = 'player' | 'weapon' | 'npc' | 'prop' | 'world' | 'other';

export interface EntitySummary {
  uid: string;
  index: number;
  class: string;
  group: EntityGroup;
  life: number;
  inPvs: boolean;
  name?: string;
  bot?: boolean;
  model?: string;
}

export interface EntitiesResult {
  tick: number;
  entities: EntitySummary[];
}

export type PropJson = null | number | string | boolean | PropJson[] | { flag: boolean; entries: Nw2Entry[] };
export interface Nw2Entry {
  key: string | number;
  type: string;
  value: PropJson;
}

export interface PropRow {
  i: number;
  name: string;
  type: string;
  value: PropJson;
  changed?: boolean;
}

export interface EntityDetail extends EntitySummary {
  serial: number;
  table: string;
  tick: number;
  changedCount: number;
  groups: Array<{ table: string; props: PropRow[] }>;
}

export interface PositionEntity {
  uid: string;
  index: number;
  group: EntityGroup;
  inPvs: boolean;
  x: number;
  y: number;
  z: number;
  yaw: number;
  pitch?: number;
  name?: string;
}

export interface PositionsResult {
  tick: number;
  localPlayer: number;
  entities: PositionEntity[];
  camera?: { tick: number; x: number; y: number; z: number; pitch: number; yaw: number };
}

export interface Lifetime {
  uid: string;
  index: number;
  serial: number;
  class?: string;
  firstTick: number;
  lastTick: number;
  deleted: boolean;
  pvs: Array<[number, number]>;
}

export interface TimelineTrackSummary {
  kind: string;
  total: number;
  counts: number[];
}

export interface TimelineSummary {
  firstTick: number;
  lastTick: number;
  readyTick: number;
  bins: number;
  complete: boolean;
  tracks: TimelineTrackSummary[];
}

export interface TimelineEvent {
  tick: number;
  kind: string;
  name: string;
  summary?: string;
  entity?: number;
}

export interface ContentItem {
  kind: string;
  name: string;
  status: 'found' | 'missing' | 'builtin' | 'not-checked' | 'workshop-installed' | 'workshop-legacy' | 'workshop-missing';
  path?: string;
  where?: string;
  source?: string;
  workshopId?: string;
  title?: string;
}

export interface ContentReport {
  summary: { found: number; missing: number; workshopInstalled: number; workshopMissing: number; notChecked: number };
  items: ContentItem[];
  map: string;
  pakfileFiles: number;
}

export interface GmodInfo {
  path: string;
  origin: 'auto' | 'manual';
  steam: string;
  mounts: Array<{ name: string; path: string; origin: string; found: boolean }>;
  gmas: number;
  vpks: number;
  files: number;
  workshopLegacy: number;
  warnings: string[];
}

export interface AppInfo {
  name: string;
  version: string;
  formatVersion: number;
  parserVersion: number;
  cacheDir: string;
  configDir: string;
  importer: string;
  importerFound: boolean;
}

export interface CacheInfo {
  dir: string;
  bytes: number;
  demos: number;
  limitBytes: number;
}

export interface RecentDemo {
  path: string;
  name: string;
  openedAt: number;
}

export type EngineEvent =
  | { type: 'import.indexed'; demo: string; firstTick: number; lastTick: number }
  | { type: 'import.progress'; demo: string; tick: number; readyTick: number; lastTick: number }
  | { type: 'import.done'; demo: string; stats: Record<string, number> }
  | { type: 'import.failed'; demo: string; error: EngineErrorInfo; readyTick?: number }
  | { type: 'settings.changed'; key: string }
  | { type: 'log'; level: 'info' | 'warn' | 'error'; message: string };
