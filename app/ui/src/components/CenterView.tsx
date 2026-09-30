import { useState } from 'react';
import { Anvil } from '../anvil';
import { call } from '../api/engine';
import type { PositionsResult } from '../api/types';
import { useQuery } from '../hooks/useQuery';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { setUi, useApp } from '../state/store';
import { formatInt } from '../util/format';
import { TopDown } from './TopDown';

export function CenterView() {
  const demoId = useApp((s) => s.demo?.id);
  const tick = useApp((s) => s.demo?.tick ?? 0);
  const playing = useApp((s) => s.demo?.playing ?? false);
  const readyTick = useApp((s) => s.demo?.imp.readyTick ?? -1);
  const selected = useApp((s) => s.demo?.selected);
  const tab = useApp((s) => s.ui.centerTab);
  const focused = useApp((s) => s.ui.focusedPanel === 'center');
  const [fitSignal, setFitSignal] = useState(0);
  const [scale, setScale] = useState(0);

  const ready = demoId !== undefined && readyTick >= 0;
  const { data, error } = useQuery<PositionsResult>(
    ready && tab === 'top' ? `${demoId}:${Math.min(tick, readyTick)}` : null,
    () => call<PositionsResult>('state.positions', { demo: demoId, tick }),
    playing ? 30 : 0,
  );

  const players = data?.entities.filter((e) => e.group === 'player').length ?? 0;
  const stats: Array<[string, React.ReactNode, 1 | 2 | 3]> = [
    [uk.center.stats.tick, formatInt(data?.tick ?? tick), 1],
    [uk.center.stats.players, String(players), 2],
    [uk.center.stats.objects, formatInt(data?.entities.length ?? 0), 3],
  ];
  if (scale > 0) stats.push([uk.center.stats.scale, `1 px = ${(1 / scale).toFixed(1)} од.`, 3]);

  return (
    <Anvil.Panel
      tabs={[
        { id: 'top', label: uk.center.topDown, icon: 'grid' },
        { id: '3d', label: uk.center.view3d, icon: 'cube' },
      ]}
      activeTab={tab}
      onTabChange={(id) => setUi({ centerTab: id as 'top' | '3d' })}
      focused={focused}
      actions={
        tab === 'top' ? (
          <Anvil.IconButton icon="zoom-fit" size="sm" label={uk.center.fit} shortcut="F" onClick={() => setFitSignal((n) => n + 1)} />
        ) : undefined
      }
      bodyStyle={{ padding: 0 }}
    >
      <div className="panel-fill" onMouseDown={() => setUi({ focusedPanel: 'center' })}>
        {tab === '3d' ? (
          <Anvil.EmptyState icon="cube" title={uk.center.renderNext}>
            {uk.center.renderNextText}
          </Anvil.EmptyState>
        ) : !ready ? (
          <Anvil.EmptyState icon="clock" title={uk.center.topDown}>
            {uk.outliner.notReady}
          </Anvil.EmptyState>
        ) : error && !data ? (
          <Anvil.EmptyState tone="error" icon="error" title={error.message} details={error.code} />
        ) : (
          <Anvil.Viewport kind="3d" view="ortho" gizmo={false} label={uk.center.topDown} stats={stats} className="viewport-fill">
            <TopDown data={data} selected={selected} onSelect={actions.select} fitSignal={fitSignal} onScale={setScale} />
          </Anvil.Viewport>
        )}
      </div>
    </Anvil.Panel>
  );
}
