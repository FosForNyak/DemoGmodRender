import { Anvil, type AnvilTypes } from '../anvil';
import { windowControls } from '../api/platform';
import { uk } from '../i18n/uk';
import * as actions from '../state/actions';
import { setUi, useApp } from '../state/store';

type MenuItem = AnvilTypes.MenuItem;

export function AppTitleBar() {
  const demoName = useApp((s) => s.demo?.name);
  const hasDemo = useApp((s) => !!s.demo);
  const recent = useApp((s) => s.recent);
  const maximized = useApp((s) => s.ui.maximized);
  const fullscreen = useApp((s) => s.ui.fullscreen);
  const centerTab = useApp((s) => s.ui.centerTab);
  const bottomTab = useApp((s) => s.ui.bottomTab);

  const recentItems: MenuItem[] = recent.length
    ? recent.slice(0, 10).map((r) => ({ label: r.name, onSelect: () => void actions.openDemo(r.path) }))
    : [{ label: uk.menu.noRecent, disabled: true }];

  const menus: AnvilTypes.MenuBarMenu[] = [
    {
      label: uk.menu.file,
      items: [
        { label: uk.menu.open, icon: 'folder-open', shortcut: 'Ctrl+O', onSelect: () => void actions.openDialog() },
        { label: uk.menu.recent, icon: 'history', items: recentItems },
        { label: uk.menu.close, shortcut: 'Ctrl+W', disabled: !hasDemo, onSelect: () => void actions.closeDemo() },
        '-',
        { label: uk.menu.settings, icon: 'settings', shortcut: 'Ctrl+,', onSelect: () => setUi({ settingsOpen: true }) },
        '-',
        { label: uk.menu.quit, shortcut: 'Alt+F4', onSelect: () => void windowControls.close() },
      ],
    },
    {
      label: uk.menu.view,
      items: [
        { label: uk.menu.topDown, checked: centerTab === 'top', shortcut: 'Ctrl+1', onSelect: () => setUi({ centerTab: 'top' }) },
        { label: uk.menu.view3d, checked: centerTab === '3d', onSelect: () => setUi({ centerTab: '3d' }) },
        '-',
        { label: uk.menu.fullscreen, icon: 'expand', shortcut: 'F11', onSelect: () => toggleFullscreen() },
        '-',
        { label: uk.menu.themeDark, onSelect: () => void actions.setSetting('ui.theme', 'dark') },
        { label: uk.menu.themeLight, onSelect: () => void actions.setSetting('ui.theme', 'light') },
      ],
    },
    {
      label: uk.menu.window,
      items: [
        { label: uk.menu.timeline, checked: bottomTab === 'timeline', shortcut: 'Ctrl+2', onSelect: () => setUi({ bottomTab: 'timeline' }) },
        { label: uk.menu.content, checked: bottomTab === 'content', shortcut: 'Ctrl+3', onSelect: () => setUi({ bottomTab: 'content' }) },
        { label: uk.menu.log, checked: bottomTab === 'log', shortcut: 'Ctrl+4', onSelect: () => setUi({ bottomTab: 'log' }) },
        '-',
        { label: uk.menu.palette, icon: 'search', shortcut: 'Ctrl+Shift+P', onSelect: () => setUi({ paletteOpen: true }) },
      ],
    },
    {
      label: uk.menu.help,
      items: [
        { label: uk.menu.locateGmod, icon: 'search', onSelect: () => void actions.locateGmod(true) },
        { label: uk.menu.about, icon: 'info', onSelect: () => setUi({ settingsOpen: true, settingsSection: 'about' }) },
      ],
    },
  ];

  return (
    <Anvil.TitleBar
      platform="windows"
      appName={uk.app.name}
      title={demoName}
      menus={menus}
      maximized={maximized}
      fullscreen={fullscreen}
      onMinimize={() => void windowControls.minimize()}
      onToggleMaximize={() => void windowControls.toggleMaximize()}
      onClose={() => void windowControls.close()}
      onExitFullscreen={() => toggleFullscreen()}
    />
  );
}

export function toggleFullscreen(): void {
  const next = !document.documentElement.dataset.fullscreen;
  if (next) document.documentElement.dataset.fullscreen = '1';
  else delete document.documentElement.dataset.fullscreen;
  setUi({ fullscreen: next });
  void windowControls.setFullscreen(next);
}
