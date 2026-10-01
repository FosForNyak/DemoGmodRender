import { useEffect } from 'react';
import { Anvil } from './anvil';
import { onFileDrop, windowControls } from './api/platform';
import { AppStatusBar } from './components/AppStatusBar';
import { AppTitleBar, toggleFullscreen } from './components/AppTitleBar';
import { BottomPanel } from './components/BottomPanel';
import { CenterView } from './components/CenterView';
import { Inspector } from './components/Inspector';
import { MainToolbar } from './components/MainToolbar';
import { Outliner } from './components/Outliner';
import { Palette } from './components/Palette';
import { ClearCacheDialog, SettingsWindow } from './components/SettingsWindow';
import { StartScreen } from './components/StartScreen';
import { uk } from './i18n/uk';
import * as actions from './state/actions';
import { getState, setUi, useApp, type DensitySetting } from './state/store';
import { formatInt } from './util/format';

function useTheme() {
  const theme = useApp((s) => (s.settings['ui.theme'] as string | undefined) ?? 'dark');
  const motion = useApp((s) => (s.settings['ui.motion'] as string | undefined) ?? 'full');
  useEffect(() => {
    const media = window.matchMedia('(prefers-color-scheme: light)');
    const apply = () => {
      document.documentElement.dataset.theme = theme === 'system' ? (media.matches ? 'light' : 'dark') : theme;
    };
    apply();
    media.addEventListener('change', apply);
    return () => media.removeEventListener('change', apply);
  }, [theme]);
  useEffect(() => {
    document.documentElement.classList.toggle('reduced-motion', motion === 'reduced');
  }, [motion]);
}

function usePlayback() {
  const playing = useApp((s) => s.demo?.playing ?? false);
  useEffect(() => {
    if (!playing) return;
    let last = performance.now();
    let carry = 0;
    let frame = requestAnimationFrame(function loop(now) {
      const s = getState();
      const demo = s.demo;
      if (!demo) return;
      carry += ((now - last) / 1000) * actions.tickRate(s);
      last = now;
      const whole = Math.floor(carry);
      if (whole > 0) {
        carry -= whole;
        const limit = actions.maxTick(s);
        const next = demo.tick + whole;
        actions.seek(next);
        if (next >= limit && (demo.imp.state === 'ready' || demo.imp.state === 'failed' || demo.imp.state === 'cancelled')) {
          actions.setPlaying(false);
          return;
        }
      }
      frame = requestAnimationFrame(loop);
    });
    return () => cancelAnimationFrame(frame);
  }, [playing]);
}

function isTyping(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el) return false;
  return el.isContentEditable || ['INPUT', 'TEXTAREA', 'SELECT'].includes(el.tagName);
}

// Shortcuts by physical key (KeyboardEvent.code), so they work in the Ukrainian layout too.
function useShortcuts() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const s = getState();
      const ctrl = e.ctrlKey || e.metaKey;
      const handled = () => {
        e.preventDefault();
        e.stopPropagation();
      };
      if (ctrl && e.shiftKey && e.code === 'KeyP') return handled(), setUi({ paletteOpen: true });
      if (ctrl && e.code === 'KeyO') return handled(), void actions.openDialog();
      if (ctrl && e.code === 'KeyW') return handled(), void actions.closeDemo();
      if (ctrl && e.code === 'Comma') return handled(), setUi({ settingsOpen: true });
      if (ctrl && e.code === 'Digit1') return handled(), setUi({ centerTab: 'top', focusedPanel: 'center' });
      if (ctrl && e.code === 'Digit2') return handled(), setUi({ bottomTab: 'timeline', focusedPanel: 'bottom' });
      if (ctrl && e.code === 'Digit3') return handled(), setUi({ bottomTab: 'content', focusedPanel: 'bottom' });
      if (ctrl && e.code === 'Digit4') return handled(), setUi({ bottomTab: 'log', focusedPanel: 'bottom' });
      if (e.code === 'F11') return handled(), toggleFullscreen();
      if (!s.demo || isTyping(e.target) || s.ui.paletteOpen || s.ui.settingsOpen) return;
      const rate = Math.round(actions.tickRate(s));
      switch (e.code) {
        case 'Space':
          return handled(), actions.setPlaying(!s.demo.playing);
        case 'ArrowLeft':
          return handled(), actions.step(e.shiftKey ? -rate : -1);
        case 'ArrowRight':
          return handled(), actions.step(e.shiftKey ? rate : 1);
        case 'Home':
          return handled(), actions.seek(0);
        case 'End':
          return handled(), actions.seek(actions.maxTick(s));
      }
    };
    window.addEventListener('keydown', onKey, true);
    return () => window.removeEventListener('keydown', onKey, true);
  }, []);
}

// Windows menu convention: F10, or Alt pressed and released alone, moves focus to the title-bar menus
// (Anvil's MenuBar then handles ← → ↓ Enter Esc); pressed again, it returns focus to the content.
function useMenuKeys() {
  useEffect(() => {
    let altAlone = false;
    let previous: HTMLElement | null = null;
    const toggle = () => {
      const active = document.activeElement as HTMLElement | null;
      if (active?.closest('.av-menubar')) {
        active.blur();
        previous?.focus();
        return;
      }
      const first = document.querySelector<HTMLElement>('.av-menubar-item');
      if (!first) return;
      previous = active;
      first.focus();
    };
    const down = (e: KeyboardEvent) => {
      altAlone = e.key === 'Alt' && !e.ctrlKey && !e.shiftKey && !e.metaKey;
      if (e.key === 'F10' && !e.ctrlKey && !e.altKey && !e.shiftKey) {
        e.preventDefault();
        toggle();
      }
    };
    const up = (e: KeyboardEvent) => {
      if (e.key === 'Alt' && altAlone) {
        e.preventDefault();
        toggle();
      }
      altAlone = false;
    };
    const reset = () => (altAlone = false);
    window.addEventListener('keydown', down, true);
    window.addEventListener('keyup', up, true);
    window.addEventListener('pointerdown', reset, true);
    return () => {
      window.removeEventListener('keydown', down, true);
      window.removeEventListener('keyup', up, true);
      window.removeEventListener('pointerdown', reset, true);
    };
  }, []);
}

function useWindowState() {
  useEffect(() => {
    let stop: (() => void) | undefined;
    const update = () => void windowControls.isMaximized().then((m) => setUi({ maximized: m }));
    update();
    void windowControls.onResized(update).then((u) => (stop = u));
    const dropStop = onFileDrop(
      (paths) => {
        const dem = paths.find((p) => p.toLowerCase().endsWith('.dem'));
        if (dem) void actions.openDemo(dem);
        else actions.log('warn', 'Перетягнутий файл не є демо (.dem)', 'app');
      },
      (over) => setUi({ dragOver: over }),
    );
    return () => {
      stop?.();
      dropStop();
    };
  }, []);
}

function Banners() {
  const gmod = useApp((s) => s.gmod);
  const closed = useApp((s) => s.ui.gmodBannerClosed);
  if (gmod.status !== 'missing' || closed) return null;
  return (
    <Anvil.InfoBar
      layout="banner"
      severity="warning"
      title={uk.banners.gmodMissing}
      action={
        <Anvil.Button size="sm" onClick={() => setUi({ settingsOpen: true, settingsSection: 'gmod' })}>
          {uk.banners.locate}
        </Anvil.Button>
      }
      closable
      onClose={() => setUi({ gmodBannerClosed: true })}
    />
  );
}

function ImportBanner() {
  const imp = useApp((s) => s.demo?.imp);
  if (!imp || (imp.state !== 'failed' && imp.state !== 'cancelled')) return null;
  const tick = formatInt(Math.max(0, imp.readyTick));
  return (
    <Anvil.InfoBar
      layout="banner"
      severity={imp.state === 'failed' ? 'danger' : 'warning'}
      title={imp.state === 'failed' ? uk.banners.importFailed(tick) : uk.banners.importCancelled(tick)}
      action={
        <>
          <Anvil.Button size="sm" variant="subtle" onClick={() => setUi({ bottomTab: 'log' })}>
            {uk.banners.showLog}
          </Anvil.Button>
          <Anvil.Button size="sm" icon="retry" onClick={() => void actions.reopenDemo()}>
            {uk.banners.retry}
          </Anvil.Button>
        </>
      }
    >
      {imp.state === 'failed' ? imp.error?.message : undefined}
    </Anvil.InfoBar>
  );
}

function DropOverlay() {
  const over = useApp((s) => s.ui.dragOver);
  const hasDemo = useApp((s) => !!s.demo);
  if (!over || !hasDemo) return null;
  return (
    <div className="drop-overlay">
      <Anvil.DropZone state="over" title={uk.start.drop} overTitle={uk.start.over} accept=".dem" icon="film" />
    </div>
  );
}

function Toasts() {
  const toasts = useApp((s) => s.toasts);
  return (
    <Anvil.ToastStack
      position="bottom-end"
      toasts={toasts.map((t) => ({ id: t.id, severity: t.severity, title: t.title, children: t.text }))}
      onDismiss={(id) => actions.dismissToast(Number(id))}
    />
  );
}

export function App() {
  useTheme();
  usePlayback();
  useShortcuts();
  useMenuKeys();
  useWindowState();
  useEffect(() => actions.init(), []);
  const hasDemo = useApp((s) => !!s.demo);
  const density = useApp((s) => (s.settings['ui.density'] as DensitySetting | undefined) ?? 'comfortable');

  return (
    <>
      <Anvil.Workspace
        density={density}
        titleBar={
          <>
            <AppTitleBar />
            <Banners />
          </>
        }
        toolbar={<MainToolbar />}
        statusBar={<AppStatusBar />}
        left={hasDemo ? <Outliner /> : undefined}
        leftIcon="list"
        leftLabel={uk.outliner.title}
        center={
          hasDemo ? (
            <div className="center-stack">
              <ImportBanner />
              <CenterView />
            </div>
          ) : (
            <StartScreen />
          )
        }
        right={hasDemo ? <Inspector /> : undefined}
        rightIcon="options"
        rightLabel={uk.inspector.title}
        bottom={hasDemo ? <BottomPanel /> : undefined}
        leftWidth="264px"
        rightWidth="340px"
        bottomHeight="240px"
      />
      <DropOverlay />
      <SettingsWindow />
      <ClearCacheDialog />
      <Palette />
      <Toasts />
    </>
  );
}
