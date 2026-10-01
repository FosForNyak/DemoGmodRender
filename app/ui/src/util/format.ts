// Number, size and time formatting per Anvil: decimal point, space before units, narrow no-break space as
// the thousands separator, timecode HH:MM:SS:FF.
const NBSP = ' ';
const THIN = ' ';

export function formatInt(n: number): string {
  const s = Math.trunc(n).toString();
  const neg = s.startsWith('-');
  const digits = neg ? s.slice(1) : s;
  const grouped = digits.replace(/\B(?=(\d{3})+(?!\d))/g, THIN);
  return neg ? `−${grouped}` : grouped;
}

export function formatFloat(n: number, digits = 2): string {
  if (!Number.isFinite(n)) return String(n);
  const fixed = n.toFixed(digits);
  return fixed.replace(/\.?0+$/, '') || '0';
}

export function formatBytes(bytes: number): string {
  const units = ['Б', 'КБ', 'МБ', 'ГБ', 'ТБ'];
  let v = bytes;
  let u = 0;
  while (v >= 1024 && u < units.length - 1) {
    v /= 1024;
    u++;
  }
  const digits = v >= 100 || u === 0 ? 0 : 1;
  return `${v.toFixed(digits)}${NBSP}${units[u]}`;
}

export function formatPercent(fraction: number): string {
  return `${Math.round(fraction * 100)}${NBSP}%`;
}

// HH:MM:SS:FF where FF counts ticks within the second (zero-padded to the width of the tick rate).
export function timecode(tick: number, tickRate: number): string {
  if (!(tickRate > 0)) return '00:00:00:00';
  const t = Math.max(0, tick);
  const seconds = Math.floor(t / tickRate);
  const frame = Math.floor(t - seconds * tickRate);
  const width = String(Math.ceil(tickRate) - 1).length;
  const pad = (n: number, w = 2) => String(n).padStart(w, '0');
  return `${pad(Math.floor(seconds / 3600))}:${pad(Math.floor(seconds / 60) % 60)}:${pad(seconds % 60)}:${pad(frame, Math.max(2, width))}`;
}

export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.round(seconds));
  const pad = (n: number) => String(n).padStart(2, '0');
  return `${pad(Math.floor(s / 3600))}:${pad(Math.floor(s / 60) % 60)}:${pad(s % 60)}`;
}

export function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v));
}
