import type { BenchmarkRun } from '../types/benchmark';
import { toNanoseconds } from '../utils/data';
import { fmtTime, ns } from '../utils/format';

interface Props {
  category: string;
  benchNames: string[];
  data: BenchmarkRun[];
  onClick: () => void;
  actionLabel?: string;
  onAction?: () => void;
}

export function SummaryCard({ category, benchNames, data, onClick, actionLabel, onAction }: Props) {
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
    <div
      className="block card card-hover p-5 no-underline text-inherit cursor-pointer"
      role="button"
      tabIndex={0}
      onClick={onClick}
      onKeyDown={(e) => {
        if (e.key === 'Enter' || e.key === ' ') {
          e.preventDefault();
          onClick();
        }
      }}
    >
      <div className="flex items-start justify-between gap-3 mb-2">
        <div className="font-bold text-[1.1rem]">{category}</div>
        {actionLabel && onAction && (
          <button
            type="button"
            className="explore-btn"
            onKeyDown={(e) => e.stopPropagation()}
            onClick={(e) => {
              e.preventDefault();
              e.stopPropagation();
              onAction();
            }}
          >
            {actionLabel}
          </button>
        )}
      </div>
      <div className="text-2xl font-extrabold text-[#333]">{fmtTime(avg)}</div>
      <div className="text-[0.9rem] text-muted mt-1">
        {cnt} benchmark{cnt !== 1 ? 's' : ''}
        {trend !== null && (
          <> &middot; <span className={`font-semibold ${trendColor}`}>{trend > 0 ? '+' : ''}{trend.toFixed(1)}%</span></>
        )}
      </div>
    </div>
  );
}
