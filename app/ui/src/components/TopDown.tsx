// Top-down view: players (dot + heading + name), props (small squares), the recording player's camera.
// Wheel zooms around the cursor, drag pans, click selects the nearest entity.
import { useCallback, useEffect, useRef, useState } from 'react';
import type { PositionEntity, PositionsResult } from '../api/types';

interface View {
  cx: number; // world point at the canvas centre
  cy: number;
  scale: number; // px per world unit
}

interface Props {
  data?: PositionsResult;
  selected?: string;
  onSelect: (uid: string) => void;
  fitSignal: number; // changes when the user asks to fit the view
  onScale?: (pxPerUnit: number) => void;
}

function cssVar(el: Element, name: string, fallback: string): string {
  return getComputedStyle(el).getPropertyValue(name).trim() || fallback;
}

function bounds(entities: PositionEntity[]): [number, number, number, number] | null {
  // Players first: the level can be much larger than where the action is.
  const players = entities.filter((e) => e.group === 'player');
  const use = players.length >= 2 ? players : entities;
  if (use.length === 0) return null;
  let x0 = Infinity;
  let y0 = Infinity;
  let x1 = -Infinity;
  let y1 = -Infinity;
  for (const e of use) {
    x0 = Math.min(x0, e.x);
    y0 = Math.min(y0, e.y);
    x1 = Math.max(x1, e.x);
    y1 = Math.max(y1, e.y);
  }
  return [x0, y0, x1, y1];
}

export function TopDown({ data, selected, onSelect, fitSignal, onScale }: Props) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [size, setSize] = useState({ w: 0, h: 0 });
  const [view, setView] = useState<View | null>(null);
  const drag = useRef<{ x: number; y: number; view: View; moved: boolean } | null>(null);
  const lastFit = useRef(-1);
  // Colours come from theme tokens read at draw time: redraw when the theme changes.
  const [themeVersion, setThemeVersion] = useState(0);

  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setSize({ w: el.clientWidth, h: el.clientHeight }));
    ro.observe(el);
    const mo = new MutationObserver(() => setThemeVersion((v) => v + 1));
    mo.observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
    return () => {
      ro.disconnect();
      mo.disconnect();
    };
  }, []);

  const fit = useCallback(() => {
    if (!data || size.w === 0) return;
    const b = bounds(data.entities);
    if (!b) return;
    const [x0, y0, x1, y1] = b;
    const w = Math.max(512, x1 - x0);
    const h = Math.max(512, y1 - y0);
    const scale = Math.min(size.w / (w * 1.2), size.h / (h * 1.2));
    setView({ cx: (x0 + x1) / 2, cy: (y0 + y1) / 2, scale });
  }, [data, size]);

  useEffect(() => {
    if ((view === null || lastFit.current !== fitSignal) && data && size.w > 0) {
      lastFit.current = fitSignal;
      fit();
    }
  }, [data, size, view, fitSignal, fit]);

  useEffect(() => {
    if (view) onScale?.(view.scale);
  }, [view, onScale]);

  const toScreen = useCallback(
    (x: number, y: number, v: View): [number, number] => [size.w / 2 + (x - v.cx) * v.scale, size.h / 2 - (y - v.cy) * v.scale],
    [size],
  );

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !view || size.w === 0) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(size.w * dpr);
    canvas.height = Math.round(size.h * dpr);
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, size.w, size.h);
    if (!data) return;
    const cText = cssVar(canvas, '--text-primary', '#e7e8e9');
    const cSecondary = cssVar(canvas, '--text-secondary', '#aeb0b3');
    const cTertiary = cssVar(canvas, '--text-tertiary', '#949699');
    const cAccent = cssVar(canvas, '--accent', '#e8a44f');
    const cInfo = cssVar(canvas, '--info', '#6aa7e8');
    const font = cssVar(canvas, '--font-sans', 'sans-serif');

    // World-aligned grid: minor lines at a power-of-two spacing at least 16 px apart, major every 4th, and
    // the world axes (the line y = 0 in the X-axis colour, x = 0 in the Y-axis colour).
    {
      let minor = 64;
      while (minor * view.scale < 16) minor *= 2;
      const major = minor * 4;
      const wx0 = view.cx - size.w / 2 / view.scale;
      const wx1 = view.cx + size.w / 2 / view.scale;
      const wy0 = view.cy - size.h / 2 / view.scale;
      const wy1 = view.cy + size.h / 2 / view.scale;
      const line = (x0: number, y0: number, x1: number, y1: number) => {
        ctx.moveTo(Math.round(x0) + 0.5, Math.round(y0) + 0.5);
        ctx.lineTo(Math.round(x1) + 0.5, Math.round(y1) + 0.5);
      };
      for (const [step, color] of [
        [minor, cssVar(canvas, '--grid-minor', '#252627')],
        [major, cssVar(canvas, '--grid-major', '#303133')],
      ] as const) {
        ctx.strokeStyle = color;
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let x = Math.ceil(wx0 / step) * step; x <= wx1; x += step) line(...toScreen(x, wy0, view), ...toScreen(x, wy1, view));
        for (let y = Math.ceil(wy0 / step) * step; y <= wy1; y += step) line(...toScreen(wx0, y, view), ...toScreen(wx1, y, view));
        ctx.stroke();
      }
      ctx.strokeStyle = cssVar(canvas, '--axis-x', '#e5484d');
      ctx.beginPath();
      line(...toScreen(wx0, 0, view), ...toScreen(wx1, 0, view));
      ctx.stroke();
      ctx.strokeStyle = cssVar(canvas, '--axis-y', '#46a758');
      ctx.beginPath();
      line(...toScreen(0, wy0, view), ...toScreen(0, wy1, view));
      ctx.stroke();
    }

    const players: PositionEntity[] = [];
    for (const e of data.entities) {
      if (e.group === 'player') {
        players.push(e);
        continue;
      }
      const [sx, sy] = toScreen(e.x, e.y, view);
      if (sx < -8 || sy < -8 || sx > size.w + 8 || sy > size.h + 8) continue;
      const isSel = e.uid === selected;
      ctx.globalAlpha = e.inPvs ? 0.9 : 0.35;
      ctx.fillStyle = isSel ? cAccent : e.group === 'prop' ? cTertiary : cSecondary;
      const r = isSel ? 4 : e.group === 'prop' ? 2 : 1.5;
      ctx.fillRect(sx - r, sy - r, r * 2, r * 2);
    }

    // Recording player's camera: a marker and a 90° field-of-view wedge.
    if (data.camera) {
      const [sx, sy] = toScreen(data.camera.x, data.camera.y, view);
      const a = (-data.camera.yaw * Math.PI) / 180;
      const len = 60;
      ctx.globalAlpha = 0.9;
      ctx.strokeStyle = cInfo;
      ctx.fillStyle = cInfo;
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(sx, sy);
      ctx.lineTo(sx + Math.cos(a - Math.PI / 4) * len, sy + Math.sin(a - Math.PI / 4) * len);
      ctx.moveTo(sx, sy);
      ctx.lineTo(sx + Math.cos(a + Math.PI / 4) * len, sy + Math.sin(a + Math.PI / 4) * len);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.beginPath();
      ctx.moveTo(sx + Math.cos(a) * 9, sy + Math.sin(a) * 9);
      ctx.lineTo(sx + Math.cos(a + 2.5) * 6, sy + Math.sin(a + 2.5) * 6);
      ctx.lineTo(sx + Math.cos(a - 2.5) * 6, sy + Math.sin(a - 2.5) * 6);
      ctx.closePath();
      ctx.fill();
    }

    ctx.font = `11px ${font}`;
    ctx.textBaseline = 'middle';
    // Names: the selected player first, then the rest; a name that would overlap one already drawn is skipped.
    const placed: Array<[number, number, number, number]> = [];
    players.sort((a, b) => (a.uid === selected ? -1 : b.uid === selected ? 1 : 0));
    for (const e of players) {
      const [sx, sy] = toScreen(e.x, e.y, view);
      if (sx < -40 || sy < -40 || sx > size.w + 40 || sy > size.h + 40) continue;
      const isSel = e.uid === selected;
      ctx.globalAlpha = e.inPvs ? 1 : 0.4;
      const color = isSel ? cAccent : cText;
      const a = (-e.yaw * Math.PI) / 180;
      ctx.strokeStyle = color;
      ctx.lineWidth = isSel ? 2 : 1.5;
      ctx.beginPath();
      ctx.moveTo(sx, sy);
      ctx.lineTo(sx + Math.cos(a) * 14, sy + Math.sin(a) * 14);
      ctx.stroke();
      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(sx, sy, isSel ? 5.5 : 4.5, 0, Math.PI * 2);
      ctx.fill();
      if (e.name) {
        const w = ctx.measureText(e.name).width;
        const box: [number, number, number, number] = [sx + 9, sy - 16, sx + 9 + w, sy - 2];
        const overlaps = placed.some((p) => box[0] < p[2] && box[2] > p[0] && box[1] < p[3] && box[3] > p[1]);
        if (!overlaps || isSel) {
          placed.push(box);
          ctx.fillStyle = isSel ? cAccent : cSecondary;
          ctx.fillText(e.name, sx + 9, sy - 9);
        }
      }
    }
    ctx.globalAlpha = 1;
    ctx.lineWidth = 1;
  }, [data, view, size, selected, toScreen, themeVersion]);

  const pick = (px: number, py: number): string | undefined => {
    if (!data || !view) return undefined;
    let best: string | undefined;
    let bestD = 12 * 12;
    for (const e of data.entities) {
      const [sx, sy] = toScreen(e.x, e.y, view);
      const d = (sx - px) ** 2 + (sy - py) ** 2 - (e.group === 'player' ? 30 : 0);
      if (d < bestD) {
        bestD = d;
        best = e.uid;
      }
    }
    return best;
  };

  return (
    <div
      ref={wrapRef}
      className="topdown"
      onWheel={(ev) => {
        if (!view || !wrapRef.current) return;
        const rect = wrapRef.current.getBoundingClientRect();
        const px = ev.clientX - rect.left;
        const py = ev.clientY - rect.top;
        const scale = Math.min(20, Math.max(0.005, view.scale * Math.exp(-ev.deltaY * 0.0015)));
        // Keep the world point under the cursor in place.
        const wx = view.cx + (px - size.w / 2) / view.scale;
        const wy = view.cy - (py - size.h / 2) / view.scale;
        setView({ scale, cx: wx - (px - size.w / 2) / scale, cy: wy + (py - size.h / 2) / scale });
      }}
      onPointerDown={(ev) => {
        if (!view) return;
        (ev.currentTarget as Element).setPointerCapture(ev.pointerId);
        drag.current = { x: ev.clientX, y: ev.clientY, view, moved: false };
      }}
      onPointerMove={(ev) => {
        const d = drag.current;
        if (!d) return;
        const dx = ev.clientX - d.x;
        const dy = ev.clientY - d.y;
        if (Math.abs(dx) + Math.abs(dy) > 3) d.moved = true;
        if (d.moved) setView({ ...d.view, cx: d.view.cx - dx / d.view.scale, cy: d.view.cy + dy / d.view.scale });
      }}
      onPointerUp={(ev) => {
        const d = drag.current;
        drag.current = null;
        if (d && !d.moved && wrapRef.current) {
          const rect = wrapRef.current.getBoundingClientRect();
          const uid = pick(ev.clientX - rect.left, ev.clientY - rect.top);
          if (uid) onSelect(uid);
        }
      }}
    >
      <canvas ref={canvasRef} style={{ width: size.w, height: size.h }} aria-label="Вигляд зверху" role="img" />
    </div>
  );
}
