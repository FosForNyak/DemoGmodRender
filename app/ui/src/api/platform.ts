// Window, file dialog and drag & drop. Paths reach the engine only from the dialog or a drop (spec §11).
import { inTauri } from './engine';

export async function pickDemoFile(title: string, filterName: string): Promise<string | null> {
  if (!inTauri) {
    // Browser development: there is no native dialog.
    return window.prompt(title);
  }
  const { open } = await import('@tauri-apps/plugin-dialog');
  const result = await open({ title, multiple: false, directory: false, filters: [{ name: filterName, extensions: ['dem'] }] });
  return typeof result === 'string' ? result : null;
}

export async function pickFolder(title: string): Promise<string | null> {
  if (!inTauri) return window.prompt(title);
  const { open } = await import('@tauri-apps/plugin-dialog');
  const result = await open({ title, directory: true, multiple: false });
  return typeof result === 'string' ? result : null;
}

// Calls `onDrop` with the paths of files dropped on the window, `onOver` while a drag hovers.
export function onFileDrop(onDrop: (paths: string[]) => void, onOver: (over: boolean) => void): () => void {
  if (!inTauri) return () => {};
  let unlisten: (() => void) | undefined;
  let cancelled = false;
  import('@tauri-apps/api/webview').then(({ getCurrentWebview }) =>
    getCurrentWebview()
      .onDragDropEvent((event) => {
        const p = event.payload;
        if (p.type === 'enter' || p.type === 'over') onOver(true);
        else if (p.type === 'leave') onOver(false);
        else if (p.type === 'drop') {
          onOver(false);
          onDrop(p.paths);
        }
      })
      .then((u) => {
        if (cancelled) u();
        else unlisten = u;
      }),
  );
  return () => {
    cancelled = true;
    unlisten?.();
  };
}

export const windowControls = {
  async minimize() {
    if (!inTauri) return;
    const { getCurrentWindow } = await import('@tauri-apps/api/window');
    await getCurrentWindow().minimize();
  },
  async toggleMaximize() {
    if (!inTauri) return;
    const { getCurrentWindow } = await import('@tauri-apps/api/window');
    await getCurrentWindow().toggleMaximize();
  },
  async close() {
    if (!inTauri) return;
    const { getCurrentWindow } = await import('@tauri-apps/api/window');
    await getCurrentWindow().close();
  },
  async setFullscreen(on: boolean) {
    if (!inTauri) {
      if (on) await document.documentElement.requestFullscreen?.();
      else if (document.fullscreenElement) await document.exitFullscreen();
      return;
    }
    const { getCurrentWindow } = await import('@tauri-apps/api/window');
    await getCurrentWindow().setFullscreen(on);
  },
  async isMaximized(): Promise<boolean> {
    if (!inTauri) return false;
    const { getCurrentWindow } = await import('@tauri-apps/api/window');
    return getCurrentWindow().isMaximized();
  },
  async onResized(handler: () => void): Promise<() => void> {
    if (!inTauri) {
      window.addEventListener('resize', handler);
      return () => window.removeEventListener('resize', handler);
    }
    const { getCurrentWindow } = await import('@tauri-apps/api/window');
    return getCurrentWindow().onResized(handler);
  },
};
