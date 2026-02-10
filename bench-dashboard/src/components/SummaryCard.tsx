import type { BenchmarkRun } from '../types/benchmark';
import { fmtTime } from '../utils/format';
import styles from './SummaryCard.module.css';

interface Props {
  category: string;
  benchNames: string[];
  data: BenchmarkRun[];
  onClick: () => void;
}

export function SummaryCard({ category, benchNames, data, onClick }: Props) {
  const latest = data[data.length - 1]!;
  const prev = data.length > 1 ? data[data.length - 2] : undefined;

  let lSum = 0, pSum = 0, cnt = 0;
  for (const name of benchNames) {
    const lb = latest.benches.find((b) => b.name === name);
    if (lb) { lSum += lb.value; cnt++; }
    if (prev) {
      const pb = prev.benches.find((b) => b.name === name);
      if (pb) pSum += pb.value;
    }
  }

  const avg = cnt ? lSum / cnt : 0;
  const trend = (prev && pSum > 0) ? (lSum - pSum) / pSum * 100 : null;

  const trendClass = trend !== null
    ? (trend > 5 ? styles.trendBad : trend < -5 ? styles.trendGood : styles.trendNeutral)
    : undefined;

  return (
    <a className={styles.card} href={`#${category}`} onClick={(e) => { e.preventDefault(); onClick(); }}>
      <div className={styles.title}>{category}</div>
      <div className={styles.value}>{fmtTime(avg)}</div>
      <div className={styles.meta}>
        {cnt} benchmark{cnt !== 1 ? 's' : ''}
        {trend !== null && (
          <> &middot; <span className={trendClass}>{trend > 0 ? '+' : ''}{trend.toFixed(1)}%</span></>
        )}
      </div>
    </a>
  );
}
