import { Link } from '@tanstack/react-router';
import { homeRoute, type OptimizerSlug } from '../router';
import { parseName, toNanoseconds } from '../utils/data';
import { fmtTime, type Nanoseconds } from '../utils/format';

interface Change {
  name: string;
  category: string;
  detail: string;
  optimizer: OptimizerSlug;
  pctChange: number;
  ratio: number;
  prevNs: Nanoseconds;
  currNs: Nanoseconds;
}

const MAX_ITEMS = 5;
const ALERT_RATIO = 1.5;

const OPTIMIZER_LABELS: Record<OptimizerSlug, string> = {
  edit: 'EditOptimizer',
  motion: 'MotionOptimizer',
  composition: 'CompositionOptimizer',
};

export function HomePage() {
  const { optimizers } = homeRoute.useLoaderData();

  const changes: Change[] = [];
  for (const { slug, data } of optimizers) {
    const runs = Object.values(data.entries)[0];
    if (!runs || runs.length < 2) continue;
    const latest = runs[runs.length - 1]!;
    const prev = runs[runs.length - 2]!;
    const prevMap = new Map(prev.benches.map((b) => [b.name, b]));

    for (const bench of latest.benches) {
      const prevBench = prevMap.get(bench.name);
      if (!prevBench) continue;
      const currNs = toNanoseconds(bench.value, bench.unit);
      const prevNs = toNanoseconds(prevBench.value, prevBench.unit);
      if (prevNs === 0) continue;
      const ratio = currNs / prevNs;
      const pctChange = (ratio - 1) * 100;
      if (Math.abs(pctChange) < 10) continue; // only show >=10% changes
      const { category, detail } = parseName(bench.name);
      changes.push({ name: bench.name, category, detail, optimizer: slug, pctChange, ratio, prevNs, currNs });
    }
  }

  const regressions = changes
    .filter((c) => c.pctChange > 0)
    .sort((a, b) => b.pctChange - a.pctChange)
    .slice(0, MAX_ITEMS);

  const improvements = changes
    .filter((c) => c.pctChange < 0)
    .sort((a, b) => a.pctChange - b.pctChange)
    .slice(0, MAX_ITEMS);

  return (
    <>
      <h1>Vimficiency Benchmarks</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">Performance tracking across commits</p>

      <h2>Optimizers</h2>
      <div className="grid grid-cols-[repeat(auto-fit,minmax(280px,1fr))] gap-4">
        {(['edit', 'motion', 'composition'] as const).map((slug) => (
          <Link
            key={slug}
            to="/$optimizer"
            params={{ optimizer: slug }}
            search={{ cat: undefined, bench: undefined }}
            className="block card card-hover p-6 no-underline text-inherit"
          >
            <div className="text-[1.4rem] font-bold text-inherit">
              {OPTIMIZER_LABELS[slug]}
            </div>
          </Link>
        ))}
      </div>

      <div className="mt-10">
        <div className="grid grid-cols-2 gap-6">
          <div>
            <h3>Largest Regressions</h3>
            {regressions.length > 0 ? (
              <div className="flex flex-col gap-1">
                {regressions.map((c) => (
                  <ChangeItem key={`${c.optimizer}-${c.name}`} change={c} />
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
                  <ChangeItem key={`${c.optimizer}-${c.name}`} change={c} />
                ))}
              </div>
            ) : (
              <p className="text-sm text-muted">None</p>
            )}
          </div>
        </div>
      </div>
    </>
  );
}

function ChangeItem({ change: c }: { change: Change }) {
  const overThreshold = c.ratio >= ALERT_RATIO || c.ratio <= 1 / ALERT_RATIO;
  const sign = c.pctChange >= 0 ? '+' : '';
  const label = c.optimizer.charAt(0).toUpperCase() + c.optimizer.slice(1);

  return (
    <Link
      to="/$optimizer"
      params={{ optimizer: c.optimizer }}
      search={{ cat: c.category, bench: c.name }}
      className="change-item no-underline"
    >
      <div className="flex flex-col gap-px min-w-0">
        <span className="font-semibold text-[0.9rem] whitespace-nowrap overflow-hidden text-ellipsis">
          {c.detail}
        </span>
        <span className="text-xs text-[#999]">
          {label} / {c.category}
        </span>
      </div>
      <div className="flex items-center gap-2 shrink-0">
        {overThreshold && (
          <span className="text-[1.1rem] text-warn" title="Exceeds alert threshold">
            {'\u26A0'}
          </span>
        )}
        <span className="text-[0.8rem] text-muted">
          {fmtTime(c.prevNs)} {'\u2192'} {fmtTime(c.currNs)}
        </span>
        <span className={`font-bold text-[0.9rem] tabular-nums ${c.pctChange > 0 ? 'text-bad' : 'text-good'}`}>
          {sign}{c.pctChange.toFixed(1)}%
        </span>
      </div>
    </Link>
  );
}
