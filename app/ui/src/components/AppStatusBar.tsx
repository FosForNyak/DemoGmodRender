import { Anvil, type AnvilTypes } from '../anvil';
import { uk } from '../i18n/uk';
import { useApp } from '../state/store';
import { formatBytes, formatDuration, formatFloat, formatPercent } from '../util/format';

export function AppStatusBar() {
  const gmod = useApp((s) => s.gmod.status);
  const imp = useApp((s) => s.demo?.imp);
  const report = useApp((s) => s.demo?.content?.report);
  const rate = useApp((s) => s.demo?.info?.tickRate);
  const duration = useApp((s) => s.demo?.info?.durationSeconds);
  const cache = useApp((s) => s.cache);

  const left: AnvilTypes.StatusItem[] = [
    gmod === 'found'
      ? { icon: 'success', tone: 'success', label: uk.status.gmodFound }
      : gmod === 'missing'
        ? { icon: 'warning', tone: 'warning', label: uk.status.gmodMissing }
        : { icon: 'search', label: uk.status.gmodSearching },
  ];
  if (imp) {
    left.push('|');
    if (imp.state === 'ready') left.push({ icon: 'checkmark', label: uk.status.importReady, priority: 2 });
    else if (imp.state === 'failed') left.push({ icon: 'error', tone: 'danger', label: imp.error?.message ?? '' });
    else if (imp.state === 'cancelled') left.push({ icon: 'warning', tone: 'warning', label: imp.error?.message ?? '' });
    else
      left.push({
        icon: 'sync',
        tone: 'accent',
        label: uk.status.importing,
        value: imp.lastTick > 0 ? formatPercent(Math.max(0, imp.readyTick) / imp.lastTick) : '…',
      });
  }
  if (report) {
    const missing = report.summary.missing + report.summary.workshopMissing;
    left.push('|', {
      icon: missing ? 'warning' : 'success',
      tone: missing ? 'warning' : 'success',
      label: uk.status.missing,
      value: String(missing),
    });
  }

  const right: AnvilTypes.StatusItem[] = [];
  if (rate) right.push({ label: uk.status.tickRate, value: formatFloat(rate, 1), priority: 2 });
  if (duration) right.push({ icon: 'timer', value: formatDuration(duration), title: 'Тривалість демо', priority: 2 });
  if (cache) right.push({ icon: 'database', label: uk.status.cache, value: formatBytes(cache.bytes), priority: 3 });

  return <Anvil.StatusBar left={left} right={right} />;
}
