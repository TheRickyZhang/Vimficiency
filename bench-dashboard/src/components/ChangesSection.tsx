import type { BenchmarkRun } from '../types/benchmark';
import { parseName } from '../utils/data';
import { fmtTime } from '../utils/format';
import styles from './ChangesSection.module.css';

const ALERT_RATIO = 1.5;
const MAX_ITEMS = 5;

interface BenchChange {
  benchName: string;
  category: string;
  detail: string;
  pctChange: number;
  ratio: number;
  latestVal: number;
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
      const ratio = lb.value / pb.value;
      const pct = (ratio - 1) * 100;
      if (Math.abs(pct) < 1) continue; // skip negligible changes
      changes.push({
        benchName: name,
        category: cat,
        detail: parseName(name).detail,
        pctChange: pct,
        ratio,
        latestVal: lb.value,
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
    <button className={styles.item} onClick={onSelect}>
      <div className={styles.itemLeft}>
        <span className={styles.detail}>{change.detail}</span>
        <span className={styles.category}>{change.category}</span>
      </div>
      <div className={styles.itemRight}>
        {overThreshold && <span className={styles.warn} title="Exceeds alert threshold">&#x26A0;</span>}
        <span className={styles.val}>{fmtTime(change.latestVal)}</span>
        <span className={isRegression ? styles.bad : styles.good}>
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
    <div className={styles.container}>
      {regressions.length > 0 && (
        <div>
          <h3 className={styles.heading}>Largest Regressions</h3>
          <div className={styles.list}>
            {regressions.map((c) => (
              <ChangeItem key={c.benchName} change={c} onSelect={() => onSelect(c.category, c.benchName)} />
            ))}
          </div>
        </div>
      )}
      {improvements.length > 0 && (
        <div>
          <h3 className={styles.heading}>Largest Improvements</h3>
          <div className={styles.list}>
            {improvements.map((c) => (
              <ChangeItem key={c.benchName} change={c} onSelect={() => onSelect(c.category, c.benchName)} />
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
