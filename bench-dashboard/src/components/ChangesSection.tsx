import type { BenchmarkRun } from '../types/benchmark';
import { parseName, toNanoseconds } from '../utils/data';
import { fmtTime, type Nanoseconds } from '../utils/format';

const ALERT_RATIO = 1.5;
const MAX_ITEMS = 3;

interface BenchChange {
  benchName: string;
  category: string;
  detail: string;
  pctChange: number;
  ratio: number;
  latestVal: Nanoseconds;
}

interface Props {
  categories: Record<string, string[]>;
  data: BenchmarkRun[];
  onSelect: (category: string, benchName: string) => void;
}

function computeChanges(categories: Record<string, string[]>, data: BenchmarkRun[]): BenchChange[] {
  if (data.length < 2) return [];
  const latest = data[data.length - 1]!;
  const prev = data[data.length - 2]!;
  const changes: BenchChange[] = [];

  for (const [cat, names] of Object.entries(categories)) {
    for (const name of names) {
      const lb = latest.benches.find((b) => b.name === name);
      const pb = prev.benches.find((b) => b.name === name);
      if (!lb || !pb || pb.value === 0) continue;
      const currNs = toNanoseconds(lb.value, lb.unit);
      const prevNs = toNanoseconds(pb.value, pb.unit);
      if (prevNs === 0) continue;
      const ratio = currNs / prevNs;
      const pct = (ratio - 1) * 100;
      if (Math.abs(pct) < 1) continue; // skip negligible changes
      changes.push({
        benchName: name,
        category: cat,
        detail: parseName(name).detail,
        pctChange: pct,
        ratio,
        latestVal: currNs,
      });
    }
  }

  return changes;
}

function ChangeItem({ change, onSelect }: { change: BenchChange; onSelect: () => void }) {
  const isRegression = change.pctChange > 0;
  const overThreshold = change.ratio >= ALERT_RATIO || change.ratio <= 1 / ALERT_RATIO;
  const sign = isRegression ? '+' : '';

  return (
    <button className="flex justify-between items-center px-3 py-2 bg-surface border border-border-light rounded-md cursor-pointer transition-[border-color,box-shadow] duration-150 text-left font-[inherit] text-inherit w-full hover:border-brand hover:shadow-[0_1px_4px_rgba(66,133,244,0.12)]" onClick={onSelect}>
      <div className="flex flex-col gap-px min-w-0">
        <span className="font-semibold text-[0.9rem] whitespace-nowrap overflow-hidden text-ellipsis">{change.detail}</span>
        <span className="text-xs text-[#999]">{change.category}</span>
      </div>
      <div className="flex items-center gap-2 shrink-0">
        {overThreshold && <span className="text-[1.1rem] text-warn" title="Exceeds alert threshold">&#x26A0;</span>}
        <span className="text-[0.8rem] text-muted">{fmtTime(change.latestVal)}</span>
        <span className={`font-bold text-[0.9rem] ${isRegression ? 'text-bad' : 'text-good'}`}>
          {sign}{change.pctChange.toFixed(1)}%
        </span>
      </div>
    </button>
  );
}

export function ChangesSection({ categories, data, onSelect }: Props) {
  const changes = computeChanges(categories, data);
  if (!changes.length) return null;

  const regressions = changes
    .filter((c) => c.pctChange > 0)
    .sort((a, b) => b.pctChange - a.pctChange)
    .slice(0, MAX_ITEMS);

  const improvements = changes
    .filter((c) => c.pctChange < 0)
    .sort((a, b) => a.pctChange - b.pctChange)
    .slice(0, MAX_ITEMS);

  if (!regressions.length && !improvements.length) return null;

  return (
    <div className="grid grid-cols-2 gap-6 mb-8 max-md:grid-cols-1">
      {regressions.length > 0 && (
        <div>
          <h3>Largest Regressions</h3>
          <div className="flex flex-col gap-1">
            {regressions.map((c) => (
              <ChangeItem key={c.benchName} change={c} onSelect={() => onSelect(c.category, c.benchName)} />
            ))}
          </div>
        </div>
      )}
      {improvements.length > 0 && (
        <div>
          <h3>Largest Improvements</h3>
          <div className="flex flex-col gap-1">
            {improvements.map((c) => (
              <ChangeItem key={c.benchName} change={c} onSelect={() => onSelect(c.category, c.benchName)} />
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
