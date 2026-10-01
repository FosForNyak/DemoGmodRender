import { useMemo, useState } from 'react';
import { Anvil, type AnvilTypes } from '../anvil';
import { call } from '../api/engine';
import type { EntitiesResult, EntityGroup, EntitySummary } from '../api/types';
import { useQuery } from '../hooks/useQuery';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { setUi, useApp } from '../state/store';

const GROUP_ORDER: EntityGroup[] = ['player', 'weapon', 'prop', 'npc', 'other', 'world'];
export const GROUP_ICON: Record<EntityGroup, AnvilTypes.IconName> = {
  player: 'person',
  weapon: 'cube',
  prop: 'cube',
  npc: 'person',
  other: 'layers',
  world: 'globe',
};

export function entityLabel(e: Pick<EntitySummary, 'name' | 'class' | 'model'>): string {
  if (e.name) return e.name;
  if (e.model && !e.model.startsWith('*')) return `${e.class} · ${e.model.split('/').pop()}`;
  return e.class;
}

export function Outliner() {
  const demoId = useApp((s) => s.demo?.id);
  const tick = useApp((s) => s.demo?.tick ?? 0);
  const playing = useApp((s) => s.demo?.playing ?? false);
  const readyTick = useApp((s) => s.demo?.imp.readyTick ?? -1);
  const selected = useApp((s) => s.demo?.selected);
  const focused = useApp((s) => s.ui.focusedPanel === 'outliner');
  const [query, setQuery] = useState('');
  const [expanded, setExpanded] = useState<Record<string, boolean>>({ player: true });

  const ready = demoId !== undefined && readyTick >= 0;
  const { data, error } = useQuery<EntitiesResult>(
    ready ? `${demoId}:${Math.min(tick, readyTick)}:${query}` : null,
    () => call<EntitiesResult>('state.entities', { demo: demoId, tick, filter: query ? { text: query } : {} }),
    playing ? 400 : 60,
  );

  const items = useMemo<AnvilTypes.TreeNode[]>(() => {
    if (!data) return [];
    const byGroup = new Map<EntityGroup, EntitySummary[]>();
    for (const e of data.entities) {
      const list = byGroup.get(e.group) ?? [];
      list.push(e);
      byGroup.set(e.group, list);
    }
    return GROUP_ORDER.filter((g) => byGroup.has(g)).map((g) => {
      const list = byGroup.get(g)!;
      if (g === 'player')
        list.sort((a, b) => (a.name ? 0 : 1) - (b.name ? 0 : 1) || (a.name ?? '').localeCompare(b.name ?? '', 'uk'));
      return {
        id: `group:${g}`,
        label: `${uk.outliner.groups[g]} (${list.length})`,
        icon: 'group',
        expanded: expanded[g] ?? query.length > 0,
        children: list.map((e) => ({
          id: e.uid,
          label: entityLabel(e),
          icon: GROUP_ICON[e.group],
          meta: `#${e.index}`,
          visible: e.inPvs,
        })),
      };
    });
  }, [data, expanded, query]);

  // Anvil TreeView copies `items` into its own state on mount and ignores later changes, so it is remounted
  // whenever the visible content changes (expansion lives in our state, selection is controlled).
  const treeKey = useMemo(() => {
    let h = 5381;
    const add = (s: string) => {
      for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0;
    };
    for (const g of items) {
      add(`${g.id}:${g.label}:${g.expanded ? 1 : 0}`);
      if (g.expanded) for (const c of g.children ?? []) add(`${c.id}:${c.label}:${c.visible ? 1 : 0}`);
    }
    return `${items.length}:${h}`;
  }, [items]);

  return (
    <Anvil.Panel
      title={uk.outliner.title}
      icon="list"
      density="compact"
      focused={focused}
      toolbar={
        <Anvil.SearchField
          size="sm"
          value={query}
          onChange={setQuery}
          onClear={() => setQuery('')}
          placeholder={uk.outliner.search}
          aria-label={uk.outliner.search}
          count={data ? data.entities.length : undefined}
        />
      }
    >
      <div className="panel-fill" onMouseDown={() => setUi({ focusedPanel: 'outliner' })}>
        {!ready ? (
          <Anvil.EmptyState size="sm" icon="clock" title={uk.outliner.title}>
            {uk.outliner.notReady}
          </Anvil.EmptyState>
        ) : error && !data ? (
          <Anvil.EmptyState size="sm" tone="error" icon="error" title={error.message} details={error.code} />
        ) : items.length === 0 && data ? (
          <Anvil.EmptyState size="sm" icon="search" title={uk.outliner.empty} />
        ) : (
          <Anvil.TreeView
            key={treeKey}
            label={uk.outliner.title}
            items={items}
            toggles={false}
            focused={focused}
            selected={selected ? [selected] : []}
            onSelect={(ids) => {
              const id = ids[ids.length - 1];
              if (!id) return;
              if (id.startsWith('group:')) {
                const g = id.slice(6);
                setExpanded((x) => ({ ...x, [g]: !(x[g] ?? query.length > 0) }));
                return;
              }
              actions.select(id);
            }}
          />
        )}
      </div>
    </Anvil.Panel>
  );
}
