import { Link } from '@tanstack/react-router';
import type { BenchmarkRun } from '../types/benchmark';
import { homeRoute, type OptimizerSlug } from '../router';
import { parseName, toNanoseconds, timeSeries, loadBenchmarkData } from '../utils/data';
import { bestUnit, fmtTime, ns } from '../utils/format';
import { BenchmarkChart } from '../components/BenchmarkChart';

interface Change {
  name: string;
  category: string;
  detail: string;
  optimizer: OptimizerSlug;
  pctChange: number;
  ratio: number;
}

interface TimingRow {
  benchName: string;
  label: string;
  latestNs: number;
  pctChange: number | null;
}

const MAX_ITEMS = 5;
const ALERT_RATIO = 1.5;
const TOP_SUITES = 6;
const TOP_CASES = 10;
const SUITE_TRENDS = 4;

const OPTIMIZER_LABELS: Partial<Record<OptimizerSlug, string>> = {
  edit: 'EditOptimizer',
  motion: 'MotionOptimizer',
  composition: 'CompositionOptimizer',
};

function benchMap(run?: BenchmarkRun): Map<string, { value: number; unit: string }> {
  const map = new Map<string, { value: number; unit: string }>();
  for (const bench of run?.benches ?? []) {
    map.set(bench.name, { value: bench.value, unit: bench.unit });
  }
  return map;
}

function computeTimingRows(runs: BenchmarkRun[], prefix: string, limit: number): TimingRow[] {
  if (runs.length === 0) return [];

  const latest = runs[runs.length - 1]!;
  const prev = runs.length > 1 ? runs[runs.length - 2] : undefined;
  const prevMap = benchMap(prev);

  const rows: TimingRow[] = [];
  for (const bench of latest.benches) {
    if (!bench.name.startsWith(prefix)) continue;
    const latestNs = Number(toNanoseconds(bench.value, bench.unit));
    const prevBench = prevMap.get(bench.name);
    let pctChange: number | null = null;
    if (prevBench) {
      const prevNs = Number(toNanoseconds(prevBench.value, prevBench.unit));
      if (prevNs > 0) {
        pctChange = ((latestNs / prevNs) - 1) * 100;
      }
    }
    rows.push({
      benchName: bench.name,
      label: bench.name.slice(prefix.length),
      latestNs,
      pctChange,
    });
  }

  return rows.sort((a, b) => b.latestNs - a.latestNs).slice(0, limit);
}

function pctLabel(value: number | null): string {
  if (value == null) return 'n/a';
  const sign = value >= 0 ? '+' : '';
  return `${sign}${value.toFixed(1)}%`;
}

export function HomePage() {
  const { optimizers } = homeRoute.useLoaderData();

  const changes: Change[] = [];
  for (const { slug, data } of optimizers) {
    if (slug === 'tests') continue;
    const runs = loadBenchmarkData(data);
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
      if (Math.abs(pctChange) < 10) continue;
      const { category, detail } = parseName(bench.name);
      changes.push({ name: bench.name, category, detail, optimizer: slug, pctChange, ratio });
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

  const testOptimizer = optimizers.find((o) => o.slug === 'tests');
  const testRuns = testOptimizer ? loadBenchmarkData(testOptimizer.data) : [];
  const testSeries = (() => {
    if (!testOptimizer) return null;
    const totalName = 'Tests/Total/All';
    const points = timeSeries(testRuns, totalName, testOptimizer.data.repoUrl);
    return points.length > 0 ? points : null;
  })();

  const suiteRows = computeTimingRows(testRuns, 'Tests/Suites/', TOP_SUITES);
  const caseRows = computeTimingRows(testRuns, 'Tests/Cases/', TOP_CASES);
  const suiteTrendRows = suiteRows.slice(0, SUITE_TRENDS);

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

      {testSeries && (
        <div className="mt-10">
          <h2>Test Duration</h2>
          <div className="max-w-[60%] mx-auto" style={{ height: 250 }}>
            <BenchmarkChart series={testSeries} unit={bestUnit(testSeries.map((p) => p.val))} />
          </div>
        </div>
      )}

      {suiteRows.length > 0 && (
        <div className="mt-10">
          <h2>Slowest Test Suites</h2>
          <div className="card p-4">
            <div className="grid grid-cols-[2fr_1fr_1fr] gap-2 text-xs font-bold text-[#666] px-2 pb-2 border-b border-border-light">
              <span>Suite</span>
              <span className="text-right">Latest</span>
              <span className="text-right">vs Prev</span>
            </div>
            <div className="flex flex-col">
              {suiteRows.map((row) => (
                <div key={row.benchName} className="grid grid-cols-[2fr_1fr_1fr] gap-2 px-2 py-2 border-b border-border-light last:border-b-0">
                  <span className="text-sm truncate" title={row.label}>{row.label}</span>
                  <span className="text-right text-sm tabular-nums">{fmtTime(ns(row.latestNs))}</span>
                  <span
                    className={`text-right text-sm tabular-nums ${
                      row.pctChange == null ? 'text-muted' : row.pctChange > 0 ? 'text-bad' : 'text-good'
                    }`}
                  >
                    {pctLabel(row.pctChange)}
                  </span>
                </div>
              ))}
            </div>
          </div>
        </div>
      )}

      {suiteTrendRows.length > 0 && testOptimizer && (
        <div className="mt-10">
          <h2>Suite Trends</h2>
          <div className="grid grid-cols-[repeat(auto-fit,minmax(260px,1fr))] gap-4">
            {suiteTrendRows.map((row) => {
              const series = timeSeries(testRuns, row.benchName, testOptimizer.data.repoUrl);
              if (series.length === 0) return null;
              return (
                <div key={row.benchName} className="card p-4">
                  <div className="font-semibold text-sm text-[#333] mb-2 truncate" title={row.label}>
                    {row.label}
                  </div>
                  <div style={{ height: 160 }}>
                    <BenchmarkChart series={series} unit={bestUnit(series.map((p) => p.val))} />
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}

      {caseRows.length > 0 && (
        <div className="mt-10">
          <h2>Slowest Tests</h2>
          <div className="card p-4">
            <div className="grid grid-cols-[2fr_1fr_1fr] gap-2 text-xs font-bold text-[#666] px-2 pb-2 border-b border-border-light">
              <span>Test</span>
              <span className="text-right">Latest</span>
              <span className="text-right">vs Prev</span>
            </div>
            <div className="flex flex-col">
              {caseRows.map((row) => (
                <div key={row.benchName} className="grid grid-cols-[2fr_1fr_1fr] gap-2 px-2 py-2 border-b border-border-light last:border-b-0">
                  <span className="text-sm truncate" title={row.label}>{row.label}</span>
                  <span className="text-right text-sm tabular-nums">{fmtTime(ns(row.latestNs))}</span>
                  <span
                    className={`text-right text-sm tabular-nums ${
                      row.pctChange == null ? 'text-muted' : row.pctChange > 0 ? 'text-bad' : 'text-good'
                    }`}
                  >
                    {pctLabel(row.pctChange)}
                  </span>
                </div>
              ))}
            </div>
          </div>
        </div>
      )}
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
        <span className={`font-bold text-[0.9rem] tabular-nums ${c.pctChange > 0 ? 'text-bad' : 'text-good'}`}>
          {sign}{c.pctChange.toFixed(1)}%
        </span>
      </div>
    </Link>
  );
}
