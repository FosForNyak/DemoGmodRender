import { useState } from 'react';
import { Anvil } from '../anvil';
import { call } from '../api/engine';
import type { EntityDetail, PropJson, PropRow } from '../api/types';
import { useQuery } from '../hooks/useQuery';
import { uk } from '../i18n/uk';
import { setUi, useApp } from '../state/store';
import { formatFloat, formatInt } from '../util/format';
import { entityLabel, GROUP_ICON } from './Outliner';

function formatValue(v: PropJson): string {
  if (v === null) return '—';
  if (typeof v === 'number') return Number.isInteger(v) ? formatInt(v) : formatFloat(v, 3);
  if (typeof v === 'boolean') return v ? 'true' : 'false';
  if (typeof v === 'string') return v.length > 160 ? `${v.slice(0, 160)}…` : v;
  if (Array.isArray(v)) {
    if (v.length === 3 && v.every((x) => typeof x === 'number')) return v.map((x) => formatFloat(x as number, 2)).join('  ');
    return `[${v.length}] ${v.slice(0, 6).map(formatValue).join(', ')}${v.length > 6 ? ', …' : ''}`;
  }
  return `${v.entries.length} ${uk.inspector.entries}`;
}

function Rows({ p }: { p: PropRow }) {
  const value = p.value;
  const rows = [
    <Anvil.PropertyGrid.Row key={p.i} label={p.name} modified={p.changed} hint={p.type} keywords={String(p.i)}>
      <span className="t-numeric prop-value" title={typeof value === 'string' ? value : undefined}>
        {formatValue(value)}
      </span>
    </Anvil.PropertyGrid.Row>,
  ];
  // GMod NW2 table: one row per variable under the prop.
  if (value && typeof value === 'object' && !Array.isArray(value)) {
    for (const e of value.entries)
      rows.push(
        <Anvil.PropertyGrid.Row key={`${p.i}:${e.key}`} label={`  ${e.key}`} modified={p.changed} hint={e.type}>
          <span className="t-numeric prop-value">{formatValue(e.value)}</span>
        </Anvil.PropertyGrid.Row>,
      );
  }
  return <>{rows}</>;
}

export function Inspector() {
  const demoId = useApp((s) => s.demo?.id);
  const tick = useApp((s) => s.demo?.tick ?? 0);
  const playing = useApp((s) => s.demo?.playing ?? false);
  const readyTick = useApp((s) => s.demo?.imp.readyTick ?? -1);
  const uid = useApp((s) => s.demo?.selected);
  const focused = useApp((s) => s.ui.focusedPanel === 'inspector');
  const [query, setQuery] = useState('');
  const [changedOnly, setChangedOnly] = useState(false);

  const { data, error } = useQuery<EntityDetail>(
    demoId && uid && readyTick >= 0 ? `${demoId}:${uid}:${Math.min(tick, readyTick)}` : null,
    () => call<EntityDetail>('state.entity', { demo: demoId, uid, tick }),
    playing ? 300 : 0,
  );
  const shown = data && data.uid === uid ? data : undefined;
  const count = shown?.groups.reduce((n, g) => n + g.props.length, 0) ?? 0;

  return (
    <Anvil.Panel
      title={uk.inspector.title}
      icon="options"
      focused={focused}
      subject={
        shown
          ? {
              icon: GROUP_ICON[shown.group],
              title: entityLabel(shown),
              meta: `#${shown.index} · ${shown.class} · ${formatInt(count)} ${uk.inspector.props}`,
              aside: shown.changedCount ? (
                <Anvil.Badge tone="accent" icon="keyframe-filled">
                  {shown.changedCount}
                </Anvil.Badge>
              ) : undefined,
            }
          : undefined
      }
      toolbar={
        shown ? (
          <div className="inspector-toolbar">
            <Anvil.SearchField size="sm" value={query} onChange={setQuery} onClear={() => setQuery('')} placeholder={uk.inspector.search} aria-label={uk.inspector.search} />
            <Anvil.SegmentedControl
              size="sm"
              label={uk.inspector.changedOnly}
              value={changedOnly ? 'changed' : 'all'}
              onChange={(v) => setChangedOnly(v === 'changed')}
              options={[
                { value: 'all', label: uk.inspector.all },
                { value: 'changed', label: uk.inspector.changedOnly },
              ]}
            />
          </div>
        ) : undefined
      }
    >
      <div className="panel-fill panel-scroll" onMouseDown={() => setUi({ focusedPanel: 'inspector' })}>
        {!uid ? (
          <Anvil.EmptyState size="sm" icon="select" title={uk.inspector.nothing}>
            {uk.inspector.nothingText}
          </Anvil.EmptyState>
        ) : error && error.code === 'entity.not_found' ? (
          <Anvil.EmptyState size="sm" icon="eye-off" title={uk.inspector.gone}>
            {uk.inspector.goneText}
          </Anvil.EmptyState>
        ) : error && !shown ? (
          <Anvil.EmptyState size="sm" tone="error" icon="error" title={error.message} details={error.code} />
        ) : shown ? (
          <>
            {!shown.inPvs && (
              <Anvil.InfoBar severity="info" title={uk.inspector.outOfPvs}>
                {uk.inspector.outOfPvsText}
              </Anvil.InfoBar>
            )}
            <Anvil.PropertyGrid query={query} filter={changedOnly ? 'modified' : 'all'} onClearQuery={() => setQuery('')}>
              {shown.groups.map((g, gi) => {
                const changed = g.props.filter((p) => p.changed).length;
                return (
                  <Anvil.PropertyGrid.Group
                    key={g.table}
                    title={g.table}
                    aside={changed ? `${changed} / ${g.props.length}` : String(g.props.length)}
                    defaultOpen={gi < 3 || changed > 0}
                  >
                    {g.props.map((p) => (
                      <Rows key={p.i} p={p} />
                    ))}
                  </Anvil.PropertyGrid.Group>
                );
              })}
            </Anvil.PropertyGrid>
          </>
        ) : null}
      </div>
    </Anvil.Panel>
  );
}
