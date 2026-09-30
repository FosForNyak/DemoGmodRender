import { useEffect } from 'react';
import { Anvil, type AnvilTypes } from '../anvil';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { getState, setUi, useApp } from '../state/store';
import { toggleFullscreen } from './AppTitleBar';

interface PaletteCommand extends AnvilTypes.Command {
  run: () => void;
}

function commands(): PaletteCommand[] {
  const s = getState();
  const hasDemo = !!s.demo;
  const list: PaletteCommand[] = [
    { id: 'open', label: uk.menu.open, icon: 'folder-open', path: [uk.menu.file], shortcut: 'Ctrl+O', run: () => void actions.openDialog() },
    { id: 'close', label: uk.menu.close, path: [uk.menu.file], shortcut: 'Ctrl+W', disabled: !hasDemo, run: () => void actions.closeDemo() },
    { id: 'settings', label: uk.menu.settings, icon: 'settings', path: [uk.menu.file], shortcut: 'Ctrl+,', run: () => setUi({ settingsOpen: true }) },
    { id: 'play', label: `${uk.toolbar.play} / ${uk.toolbar.pause}`, icon: 'play', shortcut: 'Space', disabled: !hasDemo, run: () => actions.setPlaying(!s.demo?.playing) },
    { id: 'start', label: uk.toolbar.toStart, icon: 'skip-start', shortcut: 'Home', disabled: !hasDemo, run: () => actions.seek(0) },
    { id: 'end', label: uk.toolbar.toEnd, icon: 'skip-end', shortcut: 'End', disabled: !hasDemo, run: () => actions.seek(actions.maxTick()) },
    { id: 'top', label: uk.menu.topDown, icon: 'grid', path: [uk.menu.view], shortcut: 'Ctrl+1', run: () => setUi({ centerTab: 'top' }) },
    { id: '3d', label: uk.menu.view3d, icon: 'cube', path: [uk.menu.view], run: () => setUi({ centerTab: '3d' }) },
    { id: 'fullscreen', label: uk.menu.fullscreen, icon: 'expand', path: [uk.menu.view], shortcut: 'F11', run: toggleFullscreen },
    { id: 'timeline', label: uk.menu.timeline, icon: 'clock', path: [uk.menu.window], shortcut: 'Ctrl+2', run: () => setUi({ bottomTab: 'timeline' }) },
    { id: 'content', label: uk.menu.content, icon: 'folder', path: [uk.menu.window], shortcut: 'Ctrl+3', run: () => setUi({ bottomTab: 'content' }) },
    { id: 'log', label: uk.menu.log, icon: 'terminal', path: [uk.menu.window], shortcut: 'Ctrl+4', run: () => setUi({ bottomTab: 'log' }) },
    { id: 'check', label: uk.menu.checkContent, icon: 'search', disabled: s.demo?.imp.state !== 'ready', run: () => void actions.checkContent().then(() => setUi({ bottomTab: 'content' })) },
    { id: 'gmod', label: uk.menu.locateGmod, icon: 'folder', run: () => void actions.locateGmod(true) },
    {
      id: 'theme',
      label: uk.settings.theme,
      icon: 'brush',
      path: [uk.menu.settings.replace('…', ''), uk.settings.sections.interface],
      value: (s.settings['ui.theme'] as string | undefined) === 'light' ? uk.settings.themeLight : uk.settings.themeDark,
      run: () => void actions.setSetting('ui.theme', s.settings['ui.theme'] === 'light' ? 'dark' : 'light'),
    },
  ];
  for (const r of s.recent.slice(0, 5))
    list.push({ id: `recent:${r.path}`, label: r.name, icon: 'history', group: uk.menu.recent, run: () => void actions.openDemo(r.path) });
  return list;
}

export function Palette() {
  const open = useApp((s) => s.ui.paletteOpen);
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') setUi({ paletteOpen: false });
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open]);
  if (!open) return null;
  const items = commands();
  return (
    <div className="palette-layer" onMouseDown={(e) => e.target === e.currentTarget && setUi({ paletteOpen: false })}>
      <Anvil.CommandPalette
        items={items}
        placeholder={uk.palette.placeholder}
        label={uk.menu.palette}
        onClose={() => setUi({ paletteOpen: false })}
        onRun={(c) => {
          setUi({ paletteOpen: false });
          items.find((x) => x.id === c.id)?.run();
        }}
      />
    </div>
  );
}
