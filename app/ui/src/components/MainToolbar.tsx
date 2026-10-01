import { Anvil } from '../anvil';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { useApp } from '../state/store';
import { formatInt, formatPercent, timecode } from '../util/format';

export function MainToolbar() {
  const demo = useApp((s) => s.demo);
  const rate = useApp((s) => s.demo?.info?.tickRate ?? 0);
  const imp = demo?.imp;
  const importing = imp && (imp.state === 'indexing' || imp.state === 'importing');
  const progress = imp && imp.lastTick > 0 ? Math.max(0, imp.readyTick) / imp.lastTick : undefined;
  const disabled = !demo || (imp?.readyTick ?? -1) < 0;

  return (
    <Anvil.Toolbar variant="main" label="Головний тулбар">
      <Anvil.Toolbar.Group label="Файл">
        <Anvil.Button variant="subtle" icon="folder-open" onClick={() => void actions.openDialog()} title="Ctrl+O">
          {uk.toolbar.open}
        </Anvil.Button>
      </Anvil.Toolbar.Group>
      <Anvil.Toolbar.Separator />
      <Anvil.Toolbar.Group label="Відтворення">
        <Anvil.IconButton icon="skip-start" label={uk.toolbar.toStart} shortcut="Home" disabled={disabled} onClick={() => actions.seek(0)} />
        <Anvil.IconButton icon="step-back" label={uk.toolbar.stepBack} shortcut="←" disabled={disabled} onClick={() => actions.step(-1)} />
        <Anvil.IconButton
          icon={demo?.playing ? 'pause' : 'play'}
          label={demo?.playing ? uk.toolbar.pause : uk.toolbar.play}
          shortcut="Space"
          pressed={!!demo?.playing}
          disabled={disabled}
          onClick={() => actions.setPlaying(!demo?.playing)}
        />
        <Anvil.IconButton icon="step-forward" label={uk.toolbar.stepForward} shortcut="→" disabled={disabled} onClick={() => actions.step(1)} />
        <Anvil.IconButton icon="skip-end" label={uk.toolbar.toEnd} shortcut="End" disabled={disabled} onClick={() => actions.seek(actions.maxTick())} />
      </Anvil.Toolbar.Group>
      {demo && (
        <Anvil.Toolbar.Group label="Позиція" priority={2}>
          <span className="t-numeric toolbar-readout" aria-label="Поточний тік">
            <span className="muted">{uk.toolbar.tick}</span> {formatInt(demo.tick)}
            <span className="muted"> / {formatInt(Math.max(0, demo.imp.lastTick))}</span>
          </span>
          <span className="t-numeric-strong toolbar-readout" aria-label="Таймкод">
            {timecode(demo.tick, rate)}
          </span>
        </Anvil.Toolbar.Group>
      )}
      <Anvil.Toolbar.Spacer />
      {importing && (
        <Anvil.Toolbar.Group label="Імпорт">
          <Anvil.ProgressBar
            value={imp.state === 'indexing' ? undefined : progress}
            label={imp.state === 'indexing' ? uk.toolbar.indexing : uk.toolbar.importing}
            showValue={imp.state !== 'indexing'}
            style={{ width: 200 }}
            aria-label={progress !== undefined ? `${uk.toolbar.importing} ${formatPercent(progress)}` : uk.toolbar.indexing}
          />
          <Anvil.IconButton icon="dismiss" label={uk.toolbar.cancelImport} onClick={() => void actions.cancelImport()} />
        </Anvil.Toolbar.Group>
      )}
    </Anvil.Toolbar>
  );
}
