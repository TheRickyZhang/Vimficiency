import type { BenchmarkRun } from '../types/benchmark';
import { toNanoseconds } from '../utils/data';
import { fmtTime, ns } from '../utils/format';

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
    if (lb) { lSum += toNanoseconds(lb.value, lb.unit); cnt++; }
    if (prev) {
      const pb = prev.benches.find((b) => b.name === name);
      if (pb) pSum += toNanoseconds(pb.value, pb.unit);
    }
  }

  const avg = ns(cnt ? lSum / cnt : 0);
  const trend = (prev && pSum > 0) ? (lSum - pSum) / pSum * 100 : null;

  const trendColor = trend !== null
    ? (trend > 5 ? 'text-bad' : trend < -5 ? 'text-good' : 'text-muted')
    : undefined;

  return (
    <a className="block p-5 bg-surface border border-border rounded-lg no-underline text-inherit transition-[border-color,box-shadow] duration-150 hover:border-brand hover:shadow-[0_2px_8px_rgba(66,133,244,0.12)]" href={`#${category}`} onClick={(e) => { e.preventDefault(); onClick(); }}>
      <div className="font-bold text-[1.1rem] mb-2">{category}</div>
      <div className="text-2xl font-extrabold text-[#333]">{fmtTime(avg)}</div>
      <div className="text-[0.9rem] text-muted mt-1">
        {cnt} benchmark{cnt !== 1 ? 's' : ''}
        {trend !== null && (
          <> &middot; <span className={`font-semibold ${trendColor}`}>{trend > 0 ? '+' : ''}{trend.toFixed(1)}%</span></>
        )}
      </div>
    </a>
  );
}
