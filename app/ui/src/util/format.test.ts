import { describe, expect, it } from 'vitest';
import { formatBytes, formatDuration, formatFloat, formatInt, timecode } from './format';

describe('format', () => {
  it('groups thousands with a narrow no-break space', () => {
    expect(formatInt(148959)).toBe('148 959');
    expect(formatInt(12)).toBe('12');
    expect(formatInt(-1500)).toBe('−1 500');
  });
  it('formats sizes with a space before the unit', () => {
    expect(formatBytes(512)).toBe('512 Б');
    expect(formatBytes(1536)).toBe('1.5 КБ');
    expect(formatBytes(180 * 1024 * 1024)).toBe('180 МБ');
  });
  it('builds timecodes from ticks', () => {
    expect(timecode(0, 33)).toBe('00:00:00:00');
    expect(timecode(33 * 61 + 5, 33)).toBe('00:01:01:05');
    expect(timecode(66 * 3600, 66)).toBe('01:00:00:00');
  });
  it('trims floats and formats durations', () => {
    expect(formatFloat(1.5)).toBe('1.5');
    expect(formatFloat(2)).toBe('2');
    expect(formatDuration(4513.9)).toBe('01:15:14');
  });
});
