import { useEffect, useMemo, useRef, useState } from 'react';
import { Anvil, type AnvilTypes } from '../anvil';
import { call } from '../api/engine';
import type { ContentItem, Lifetime, TimelineEvent, TimelineSummary } from '../api/types';
import { useQuery } from '../hooks/useQuery';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { setUi, useApp, type AppState } from '../state/store';
import { formatInt } from '../util/format';

const WINDOW_SECONDS = 10;
const TRACK_COLUMN_PX = 200;

function useWidth(): [React.RefObject<HTMLDivElement>, number] {
  const ref = useRef<HTMLDivElement>(null);
  const [w, setW] = useState(0);
  useEffect(() => {
    const el = ref.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setW(el.clientWidth));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);
  return [ref, w];
}

function TimelineTab() {
  const demo = useApp((s) => s.demo);
  const rate = useApp((s) => s.demo?.info?.tickRate ?? 66);
  const [ref, width] = useWidth();
  const id = demo?.id;
  const imp = demo?.imp;
  const tick = demo?.tick ?? 0;
  const lastTick = Math.max(1, imp?.lastTick ?? 1);
  const readyTick = imp?.readyTick ?? -1;

  // A window of WINDOW_SECONDS around the playhead, moved in quarter steps so it is not refetched every tick.
  const span = Math.max(66, Math.round(WINDOW_SECONDS * rate));
  const quarter = Math.max(1, Math.round(span / 4));
  const start = Math.max(0, Math.min(Math.floor((tick - span / 3) / quarter) * quarter, lastTick - span));
  const end = start + span;

  const summary = useQuery<TimelineSummary>(
    id && lastTick > 1 ? `${id}:${imp?.state === 'ready' ? 'done' : Math.floor(readyTick / 5000)}` : null,
    () => call<TimelineSummary>('timeline.summary', { demo: id, bins: 200 }),
  );
  const events = useQuery<{ events: TimelineEvent[]; truncated: boolean }>(
    id && readyTick >= 0 ? `${id}:${start}:${end}:${Math.min(readyTick, end)}` : null,
    () => call('timeline.events', { demo: id, from: start, to: end, limit: 5000 }),
    200,
  );
  const life = useQuery<Lifetime>(
    id && demo?.selected && imp?.state === 'ready' ? `${id}:${demo.selected}` : null,
    () => call<Lifetime>('entity.lifetime', { demo: id, uid: demo?.selected }),
  );

  const tracks = useMemo<AnvilTypes.TimelineTrack[]>(() => {
    const out: AnvilTypes.TimelineTrack[] = [];
    if (life.data && life.data.uid === demo?.selected) {
      out.push({
        id: 'life',
        name: uk.timeline.lives,
        kind: 'video',
        clips: life.data.pvs
          .filter(([a, b]) => b >= start && a <= end)
          .map(([a, b]) => ({ start: Math.max(a, start), end: Math.min(b, end), label: `#${life.data!.index}` })),
      });
    }
    const totals = new Map(summary.data?.tracks.map((t) => [t.kind, t.total]) ?? []);
    const byKind = new Map<string, number[]>();
    for (const e of events.data?.events ?? []) {
      const list = byKind.get(e.kind) ?? [];
      list.push(e.tick);
      byKind.set(e.kind, list);
    }
    const kinds = [...new Set([...totals.keys(), ...byKind.keys()])];
    for (const kind of kinds) {
      out.push({
        id: kind,
        name: `${uk.timeline.tracks[kind] ?? kind} · ${formatInt(totals.get(kind) ?? byKind.get(kind)?.length ?? 0)}`,
        kind: 'keys',
        keys: byKind.get(kind) ?? [],
      });
    }
    return out;
  }, [events.data, summary.data, life.data, demo?.selected, start, end]);

  if (!demo || !imp) return null;
  const fps = Math.max(1, Math.round(rate));
  const cached: Array<[number, number]> = readyTick >= 0 ? [[imp.firstTick, Math.min(readyTick, lastTick)]] : [];
  return (
    <div className="timeline-tab" ref={ref}>
      <Anvil.Scrubber
        fps={fps}
        start={0}
        end={lastTick}
        value={tick}
        onChange={actions.seek}
        cached={cached}
        showTime
        label={uk.bottom.timeline}
      />
      <div className="timeline-body">
        <Anvil.Timeline
          fps={fps}
          start={start}
          end={end}
          pxPerFrame={Math.max(0.01, (width - TRACK_COLUMN_PX) / span)}
          current={tick}
          onSeek={actions.seek}
          tracks={tracks}
          label={uk.bottom.timeline}
        />
      </div>
    </div>
  );
}

const STATUS_BADGE: Record<ContentItem['status'], { tone: AnvilTypes.BadgeProps['tone']; icon: AnvilTypes.IconName }> = {
  found: { tone: 'success', icon: 'success' },
  missing: { tone: 'danger', icon: 'error' },
  builtin: { tone: 'neutral', icon: 'checkmark' },
  'not-checked': { tone: 'neutral', icon: 'info' },
  'workshop-installed': { tone: 'success', icon: 'success' },
  'workshop-legacy': { tone: 'warning', icon: 'warning' },
  'workshop-missing': { tone: 'danger', icon: 'error' },
};

function ContentTab() {
  const content = useApp((s) => s.demo?.content);
  const importState = useApp((s) => s.demo?.imp.state);
  const gmod = useApp((s) => s.gmod.status);
  const [filter, setFilter] = useState<'all' | 'missing' | 'workshop'>('missing');

  const rows = useMemo<AnvilTypes.TableRow[]>(() => {
    const items = content?.report?.items ?? [];
    return items
      .filter((i) =>
        filter === 'missing'
          ? i.status === 'missing' || i.status === 'workshop-missing' || i.status === 'workshop-legacy'
          : filter === 'workshop'
            ? i.kind === 'workshop'
            : true,
      )
      .map((i, n) => ({
        id: `${n}:${i.kind}:${i.name}`,
        name: i.path ?? i.name,
        kind: uk.content.kinds[i.kind] ?? i.kind,
        status: i.status,
        where: [i.where, i.title ?? i.source].filter(Boolean).join(' · '),
      }));
  }, [content?.report, filter]);

  if (importState !== 'ready')
    return (
      <Anvil.EmptyState size="sm" icon="clock" title={uk.content.notReady}>
        {uk.content.notReadyText}
      </Anvil.EmptyState>
    );
  if (gmod === 'missing')
    return (
      <Anvil.EmptyState
        size="sm"
        tone="error"
        icon="folder"
        title={uk.content.gmodMissing}
        actions={<Anvil.Button onClick={() => setUi({ settingsOpen: true, settingsSection: 'gmod' })}>{uk.content.locate}</Anvil.Button>}
      >
        {uk.content.gmodMissingText}
      </Anvil.EmptyState>
    );
  if (!content || content.status === 'idle')
    return (
      <Anvil.EmptyState size="sm" icon="search" title={uk.content.check} actions={<Anvil.Button onClick={() => void actions.checkContent()}>{uk.content.check}</Anvil.Button>} />
    );
  if (content.status === 'loading') return <Anvil.Spinner block label={uk.content.checking} />;
  if (content.status === 'error' || !content.report)
    return (
      <Anvil.EmptyState
        size="sm"
        tone="error"
        icon="error"
        title={content.error?.message ?? ''}
        details={content.error?.code}
        actions={<Anvil.Button icon="retry" onClick={() => void actions.checkContent()}>{uk.banners.retry}</Anvil.Button>}
      />
    );

  const s = content.report.summary;
  const wsTotal = s.workshopInstalled + s.workshopMissing;
  return (
    <div className="content-tab">
      <div className="content-header">
        <span className="t-caption muted">{uk.content.summary(s.found, s.missing, s.workshopInstalled, wsTotal)}</span>
        <Anvil.SegmentedControl
          size="sm"
          label="Фільтр"
          value={filter}
          onChange={(v) => setFilter(v as typeof filter)}
          options={[
            { value: 'missing', label: `${uk.content.filterMissing} · ${s.missing + s.workshopMissing}` },
            { value: 'workshop', label: uk.content.filterWorkshop },
            { value: 'all', label: uk.content.filterAll },
          ]}
        />
      </div>
      <Anvil.DataTable
        density="compact"
        label={uk.bottom.content}
        rows={rows}
        empty={uk.content.empty}
        defaultSort={{ key: 'name', dir: 'asc' }}
        columns={[
          { key: 'name', label: uk.content.columns.name, mono: true, sortable: true },
          { key: 'kind', label: uk.content.columns.kind, width: 140, sortable: true, secondary: true },
          {
            key: 'status',
            label: uk.content.columns.status,
            width: 170,
            sortable: true,
            render: (r) => {
              const b = STATUS_BADGE[r.status as ContentItem['status']];
              return (
                <Anvil.Badge tone={b.tone} icon={b.icon}>
                  {uk.content.status[r.status]}
                </Anvil.Badge>
              );
            },
          },
          { key: 'where', label: uk.content.columns.where, width: 280, secondary: true },
        ]}
      />
    </div>
  );
}

function LogTab() {
  const log = useApp((s) => s.log);
  return (
    <Anvil.LogViewer
      label={uk.bottom.log}
      height="100%"
      defaultFollow
      lines={log.map((l) => ({ id: l.id, time: l.time, level: l.level, source: l.source, text: l.text }))}
      onClear={() => {
        /* the log is kept for the session */
      }}
    />
  );
}

const missingCount = (s: AppState) => {
  const r = s.demo?.content?.report?.summary;
  return r ? r.missing + r.workshopMissing : undefined;
};
const errorCount = (s: AppState) => s.log.filter((l) => l.level === 'error').length;

export function BottomPanel() {
  const tab = useApp((s) => s.ui.bottomTab);
  const missing = useApp(missingCount);
  const errors = useApp(errorCount);
  const focused = useApp((s) => s.ui.focusedPanel === 'bottom');
  return (
    <Anvil.Panel
      density="compact"
      focused={focused}
      tabs={[
        { id: 'timeline', label: uk.bottom.timeline, icon: 'clock' },
        { id: 'content', label: uk.bottom.content, icon: 'folder', count: missing },
        { id: 'log', label: uk.bottom.log, icon: 'terminal', count: errors || undefined },
      ]}
      activeTab={tab}
      onTabChange={(id) => setUi({ bottomTab: id as AppState['ui']['bottomTab'] })}
      bodyStyle={{ padding: 0 }}
    >
      <div className="panel-fill" onMouseDown={() => setUi({ focusedPanel: 'bottom' })}>
        {tab === 'timeline' ? <TimelineTab /> : tab === 'content' ? <ContentTab /> : <LogTab />}
      </div>
    </Anvil.Panel>
  );
}
