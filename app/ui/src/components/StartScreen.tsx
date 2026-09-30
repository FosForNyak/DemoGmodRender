import { Anvil } from '../anvil';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { useApp } from '../state/store';

export function StartScreen() {
  const recent = useApp((s) => s.recent);
  const dragOver = useApp((s) => s.ui.dragOver);
  const opening = useApp((s) => s.opening);
  const openError = useApp((s) => s.openError);

  return (
    <div className="start-screen" data-density="spacious">
      <div className="start-column">
        <h1 className="t-display start-title">{uk.start.title}</h1>
        {openError && (
          <Anvil.InfoBar severity="danger" title={uk.banners.openFailed}>
            {openError.message}
            {openError.details ? ` — ${openError.details}` : ''}
          </Anvil.InfoBar>
        )}
        <Anvil.DropZone
          state={opening ? 'uploading' : dragOver ? 'over' : 'idle'}
          title={uk.start.drop}
          description={uk.start.dropDescription}
          overTitle={uk.start.over}
          accept=".dem"
          icon="film"
          browseLabel={uk.start.browse}
          onBrowse={() => void actions.openDialog()}
          progressLabel={opening ? `${uk.start.opening} ${opening.split(/[\\/]/).pop()}` : undefined}
          files={opening ? [{ name: opening.split(/[\\/]/).pop() ?? opening }] : undefined}
        />
        <section className="start-recent">
          <h2 className="t-ui-strong">{uk.start.recent}</h2>
          {recent.length === 0 ? (
            <p className="t-ui muted">{uk.start.noRecent}</p>
          ) : (
            <Anvil.ListBox
              label={uk.start.recent}
              items={recent.map((r) => ({
                value: r.path,
                label: r.name,
                icon: 'film' as const,
                meta: new Date(r.openedAt * 1000).toLocaleDateString('uk-UA'),
              }))}
              value={null}
              onChange={(path: string) => void actions.openDemo(path)}
              onOpen={(item) => void actions.openDemo(item.value)}
            />
          )}
        </section>
      </div>
    </div>
  );
}
