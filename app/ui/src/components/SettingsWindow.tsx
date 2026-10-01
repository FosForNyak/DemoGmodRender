// Settings (Ctrl+,): Anvil Settings in a ToolWindow over the main window. Changes apply immediately.
import { useEffect, useState } from 'react';
import { Anvil } from '../anvil';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { setUi, useApp } from '../state/store';
import { formatBytes, formatInt } from '../util/format';

const GB = 1024 ** 3;

export function SettingsWindow() {
  const open = useApp((s) => s.ui.settingsOpen);
  const section = useApp((s) => s.ui.settingsSection);
  const settings = useApp((s) => s.settings);
  const gmod = useApp((s) => s.gmod);
  const cache = useApp((s) => s.cache);
  const appInfo = useApp((s) => s.appInfo);
  const [maximized, setMaximized] = useState(false);

  useEffect(() => {
    if (!open) return;
    void actions.refreshCache();
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setUi({ settingsOpen: false });
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open]);

  if (!open) return null;
  const theme = (settings['ui.theme'] as string | undefined) ?? 'dark';
  const density = (settings['ui.density'] as string | undefined) ?? 'comfortable';
  const motion = (settings['ui.motion'] as string | undefined) ?? 'full';
  const gmodPath = (settings['gmod.path'] as string | undefined) ?? '';
  const limit = cache?.limitBytes ?? 20 * GB;
  const close = () => setUi({ settingsOpen: false });

  return (
    <div className={maximized ? 'tool-window-layer maximized' : 'tool-window-layer'}>
      <Anvil.ToolWindow
        title={uk.settings.title}
        icon="settings"
        platform="windows"
        dockable={false}
        maximized={maximized}
        onClose={close}
        onMinimize={close}
        onToggleMaximize={() => setMaximized((m) => !m)}
        density="comfortable"
      >
        <Anvil.Settings
          value={section ?? 'interface'}
          onChange={(id) => setUi({ settingsSection: id })}
          sections={[
            { id: 'interface', label: uk.settings.sections.interface, icon: 'brush', group: uk.settings.groups.app, noScope: true },
            {
              id: 'gmod',
              label: uk.settings.sections.gmod,
              icon: 'folder',
              group: uk.settings.groups.app,
              noScope: true,
              status: gmod.status === 'missing' ? 'warning' : undefined,
            },
            { id: 'cache', label: uk.settings.sections.cache, icon: 'database', group: uk.settings.groups.system, noScope: true },
            { id: 'about', label: uk.settings.sections.about, icon: 'info', group: uk.settings.groups.system, noScope: true },
          ]}
        >
          <Anvil.Settings.Section section="interface" title={uk.settings.sections.interface}>
            <Anvil.Settings.Row
              label={uk.settings.theme}
              description={uk.settings.themeDescription}
              id="ui.theme"
              source={settings['ui.theme'] ? 'profile' : 'default'}
              resetTo={uk.settings.themeDark}
              onReset={settings['ui.theme'] ? () => void actions.setSetting('ui.theme', null) : null}
            >
              <Anvil.SegmentedControl
                label={uk.settings.theme}
                value={theme}
                onChange={(v) => void actions.setSetting('ui.theme', v)}
                options={[
                  { value: 'dark', label: uk.settings.themeDark },
                  { value: 'light', label: uk.settings.themeLight },
                  { value: 'system', label: uk.settings.themeSystem },
                ]}
              />
            </Anvil.Settings.Row>
            <Anvil.Settings.Row
              label={uk.settings.density}
              description={uk.settings.densityDescription}
              id="ui.density"
              source={settings['ui.density'] ? 'profile' : 'default'}
              resetTo={uk.settings.densityComfortable}
              onReset={settings['ui.density'] ? () => void actions.setSetting('ui.density', null) : null}
            >
              <Anvil.SegmentedControl
                label={uk.settings.density}
                value={density}
                onChange={(v) => void actions.setSetting('ui.density', v)}
                options={[
                  { value: 'compact', label: uk.settings.densityCompact },
                  { value: 'comfortable', label: uk.settings.densityComfortable },
                  { value: 'spacious', label: uk.settings.densitySpacious },
                ]}
              />
            </Anvil.Settings.Row>
            <Anvil.Settings.Row
              label={uk.settings.motion}
              description={uk.settings.motionDescription}
              id="ui.motion"
              source={settings['ui.motion'] ? 'profile' : 'default'}
              resetTo={uk.settings.motionFull}
              onReset={settings['ui.motion'] ? () => void actions.setSetting('ui.motion', null) : null}
            >
              <Anvil.SegmentedControl
                label={uk.settings.motion}
                value={motion}
                onChange={(v) => void actions.setSetting('ui.motion', v)}
                options={[
                  { value: 'full', label: uk.settings.motionFull },
                  { value: 'reduced', label: uk.settings.motionReduced },
                ]}
              />
            </Anvil.Settings.Row>
          </Anvil.Settings.Section>

          <Anvil.Settings.Section section="gmod" title={uk.settings.sections.gmod}>
            <Anvil.Settings.Row
              label={uk.settings.gmodPath}
              description={uk.settings.gmodPathDescription}
              id="gmod.path"
              machine
              layout="stack"
              source={gmodPath ? 'profile' : 'default'}
              resetTo={uk.settings.gmodAuto}
              onReset={gmodPath ? () => void actions.setSetting('gmod.path', null) : null}
              warning={gmod.status === 'missing' ? gmod.error?.message : undefined}
            >
              <div className="settings-stack">
                <Anvil.PathField
                  kind="folder"
                  value={gmodPath || gmod.info?.path || ''}
                  placeholder={uk.settings.gmodAuto}
                  missing={gmod.status === 'missing'}
                  onBrowse={() => void actions.browseGmodFolder()}
                  onLocate={() => void actions.browseGmodFolder()}
                  badge={gmodPath ? undefined : 'авто'}
                  aria-label={uk.settings.gmodPath}
                />
                <div className="settings-inline">
                  <Anvil.Button size="sm" icon="search" onClick={() => void actions.setSetting('gmod.path', null)}>
                    {uk.settings.gmodAuto}
                  </Anvil.Button>
                  {gmod.info && (
                    <span className="t-caption muted">
                      {uk.settings.gmodStats(gmod.info.gmas, gmod.info.vpks, formatInt(gmod.info.files))}
                    </span>
                  )}
                </div>
              </div>
            </Anvil.Settings.Row>
          </Anvil.Settings.Section>

          <Anvil.Settings.Section section="cache" title={uk.settings.sections.cache}>
            <Anvil.Settings.Row
              label={uk.settings.cacheLimit}
              description={uk.settings.cacheLimitDescription}
              id="cache.limitBytes"
              machine
              source={settings['cache.limitBytes'] !== undefined ? 'profile' : 'default'}
              resetTo="20 ГБ"
              onReset={settings['cache.limitBytes'] !== undefined ? () => void actions.setSetting('cache.limitBytes', null) : null}
            >
              <Anvil.NumberField
                value={Math.round((limit / GB) * 10) / 10}
                min={1}
                max={1000}
                step={1}
                precision={0}
                unit="ГБ"
                spin
                aria-label={uk.settings.cacheLimit}
                onChange={(v) => void actions.setSetting('cache.limitBytes', Math.round(v * GB))}
              />
            </Anvil.Settings.Row>
            <Anvil.Settings.Row label={uk.settings.cacheUsage} layout="stack">
              <div className="settings-stack">
                <Anvil.Meter
                  value={cache ? Math.min(1, cache.bytes / limit) : 0}
                  label={cache ? `${formatBytes(cache.bytes)} з ${formatBytes(limit)}` : '…'}
                  detail={cache ? `${cache.demos} демо` : undefined}
                />
                <div className="settings-inline">
                  <Anvil.Button size="sm" variant="danger" icon="delete" onClick={() => setUi({ clearCacheDialog: true })} disabled={!cache || cache.demos === 0}>
                    {uk.settings.cacheClear}
                  </Anvil.Button>
                </div>
              </div>
            </Anvil.Settings.Row>
          </Anvil.Settings.Section>

          <Anvil.Settings.Section section="about" title={uk.settings.sections.about}>
            <Anvil.Settings.Row label={uk.settings.version}>
              <span className="t-numeric">{appInfo?.version ?? '—'}</span>
            </Anvil.Settings.Row>
            <Anvil.Settings.Row label={uk.settings.formats}>
              <span className="t-numeric">
                state.gmstate v{appInfo?.formatVersion ?? '—'} · парсер v{appInfo?.parserVersion ?? '—'}
              </span>
            </Anvil.Settings.Row>
            <Anvil.Settings.Row label={uk.settings.cacheDir} layout="stack">
              <span className="t-numeric selectable">{appInfo?.cacheDir ?? '—'}</span>
            </Anvil.Settings.Row>
            <Anvil.Settings.Row label={uk.settings.configDir} layout="stack">
              <span className="t-numeric selectable">{appInfo?.configDir ?? '—'}</span>
            </Anvil.Settings.Row>
          </Anvil.Settings.Section>
        </Anvil.Settings>
      </Anvil.ToolWindow>
    </div>
  );
}

export function ClearCacheDialog() {
  const open = useApp((s) => s.ui.clearCacheDialog);
  const cache = useApp((s) => s.cache);
  return (
    <Anvil.Dialog
      open={open}
      tone="danger"
      size="sm"
      title={uk.settings.cacheClearTitle}
      onClose={() => setUi({ clearCacheDialog: false })}
      footer={
        <>
          <Anvil.Button onClick={() => setUi({ clearCacheDialog: false })}>{uk.settings.cancel}</Anvil.Button>
          <Anvil.Button variant="danger" onClick={() => void actions.clearCache()}>
            {uk.settings.clear}
          </Anvil.Button>
        </>
      }
    >
      <p className="t-body">{uk.settings.cacheClearText(cache?.demos ?? 0, formatBytes(cache?.bytes ?? 0))}</p>
    </Anvil.Dialog>
  );
}
