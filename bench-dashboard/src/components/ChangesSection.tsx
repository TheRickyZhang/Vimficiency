import type { BenchmarkRun } from '../types/benchmark';
import { parseName, toNanoseconds } from '../utils/data';

const ALERT_RATIO = 1.5;
const MAX_ITEMS = 3;

interface BenchChange {
  benchName: string;
  category: string;
  detail: string;
  pctChange: number;
  ratio: number;
  relativeVal: number;
}

interface Props {
  categories: Record<string, string[]>;
  data: BenchmarkRun[];
  onSelect: (category: string, benchName: string) => void;
}

function computeChanges(categories: Record<string, string[]>, data: BenchmarkRun[]): BenchChange[] {
  if (data.length < 2) return [];
  const latest = data[data.length - 1]!;
  const first = data[0]!;
  const changes: BenchChange[] = [];

  for (const [cat, names] of Object.entries(categories)) {
    for (const name of names) {
      const lb = latest.benches.find((b) => b.name === name);
      const fb = first.benches.find((b) => b.name === name);
      if (!lb || !fb) continue;
      const currNs = toNanoseconds(lb.value, lb.unit);
      const baseNs = toNanoseconds(fb.value, fb.unit);
      if (baseNs === 0) continue;
      const ratio = currNs / baseNs;
      const pct = (ratio - 1) * 100;
      if (Math.abs(pct) < 10) continue; // only show >=10% changes
      changes.push({
        benchName: name,
        category: cat,
        detail: parseName(name).detail,
        pctChange: pct,
        ratio,
        relativeVal: ratio,
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
    <button className="change-item cursor-pointer text-left font-[inherit] w-full" onClick={onSelect}>
      <div className="flex flex-col gap-px min-w-0">
        <span className="font-semibold text-[0.9rem] whitespace-nowrap overflow-hidden text-ellipsis">{change.detail}</span>
        <span className="text-xs text-[#999]">{change.category}</span>
      </div>
      <div className="flex items-center gap-2 shrink-0">
        {overThreshold && <span className="text-[1.1rem] text-warn" title="Exceeds alert threshold">&#x26A0;</span>}
        <span className="text-[0.8rem] text-muted">{change.relativeVal.toFixed(2)}&times;</span>
        <span className={`font-bold text-[0.9rem] ${isRegression ? 'text-bad' : 'text-good'}`}>
          {sign}{change.pctChange.toFixed(1)}%
        </span>
      </div>
    </button>
  );
}

export function ChangesSection({ categories, data, onSelect }: Props) {
  const changes = computeChanges(categories, data);

  const regressions = changes
    .filter((c) => c.pctChange > 0)
    .sort((a, b) => b.pctChange - a.pctChange)
    .slice(0, MAX_ITEMS);

  const improvements = changes
    .filter((c) => c.pctChange < 0)
    .sort((a, b) => a.pctChange - b.pctChange)
    .slice(0, MAX_ITEMS);

  return (
    <div className="grid grid-cols-2 gap-6 mb-8 max-md:grid-cols-1">
      <div>
        <h3>Largest Regressions</h3>
        {regressions.length > 0 ? (
          <div className="flex flex-col gap-1">
            {regressions.map((c) => (
              <ChangeItem key={c.benchName} change={c} onSelect={() => onSelect(c.category, c.benchName)} />
            ))}
          </div>
        ) : (
          <p className="text-sm text-muted">None</p>
        )}
      </div>
      <div>
        <h3>Largest Improvements</h3>
        {improvements.length > 0 ? (
          <div className="flex flex-col gap-1">
            {improvements.map((c) => (
              <ChangeItem key={c.benchName} change={c} onSelect={() => onSelect(c.category, c.benchName)} />
            ))}
          </div>
        ) : (
          <p className="text-sm text-muted">None</p>
        )}
      </div>
    </div>
  );
}
