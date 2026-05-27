import { Link } from '@tanstack/react-router';
import { homeRoute, type OptimizerSlug } from '../router';
import { parseName, toNanoseconds, timeSeries, loadBenchmarkData } from '../utils/data';
import { bestUnit, fmtTime } from '../utils/format';
import { TEST_SUITES, summarizeTestSuite, type TestSuiteSummary } from '../utils/testSuites';
import { BenchmarkChart } from '../components/BenchmarkChart';

interface Change {
  name: string;
  category: string;
  detail: string;
  optimizer: OptimizerSlug;
  pctChange: number;
  ratio: number;
}

const MAX_ITEMS = 5;
const ALERT_RATIO = 1.5;

const OPTIMIZER_LABELS: Partial<Record<OptimizerSlug, string>> = {
  edit: 'TransformOptimizer',
  motion: 'NavOptimizer',
  composition: 'CompositionOptimizer',
};

export function HomePage() {
  const { optimizers } = homeRoute.useLoaderData();

  const changes: Change[] = [];
  for (const { slug, data } of optimizers) {
    if (slug === 'tests') continue;
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
  const testSummaries = testOptimizer
    ? TEST_SUITES.map((suite) => summarizeTestSuite(testOptimizer.data, suite.slug) ?? {
      slug: suite.slug,
      label: suite.label,
      title: suite.title,
      latestDuration: null,
      avgSuiteDuration: null,
      trend: null,
      suiteCount: 0,
    })
    : [];
  const testSeries = (() => {
    if (!testOptimizer) return null;
    const runs = loadBenchmarkData(testOptimizer.data);
    const totalName = 'Tests/Total/All';
    const points = timeSeries(runs, totalName, testOptimizer.data.repoUrl);
    return points.length > 0 ? points : null;
  })();

  return (
    <>
      <h1>Vimficiency Benchmarks</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">Performance tracking across commits</p>

      <h2>Optimizer Benchmarks</h2>
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

      {testSummaries.length > 0 && (
        <div className="mt-10">
          <h2>Tests</h2>
          <div className="grid grid-cols-[repeat(auto-fit,minmax(220px,1fr))] gap-4">
            {testSummaries.map((summary) => (
              <TestSuiteCard key={summary.slug} summary={summary} />
            ))}
          </div>
        </div>
      )}

      {testSeries && (
        <div className="mt-10 max-w-[60%] mx-auto max-md:max-w-full">
          <h2>Total C++ Test Duration</h2>
          <div style={{ height: 250 }}>
            <BenchmarkChart series={testSeries} unit={bestUnit(testSeries.map((p) => p.val))} />
          </div>
        </div>
      )}

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

function TestSuiteCard({ summary }: { summary: TestSuiteSummary }) {
  const trendColor = summary.trend !== null
    ? (summary.trend > 5 ? 'text-bad' : summary.trend < -5 ? 'text-good' : 'text-muted')
    : undefined;

  return (
    <Link
      to="/$optimizer/$testSuite"
      params={{ optimizer: 'tests', testSuite: summary.slug }}
      search={{ cat: undefined, bench: undefined }}
      className="block card card-hover p-5 no-underline text-inherit"
    >
      <div className="font-bold text-[1.1rem] mb-2">{summary.title}</div>
      <div className="text-2xl font-extrabold text-[#333]">
        {summary.latestDuration !== null ? fmtTime(summary.latestDuration) : 'No data'}
      </div>
      <div className="text-[0.9rem] text-muted mt-1">
        {summary.avgSuiteDuration !== null
          ? `avg suite ${fmtTime(summary.avgSuiteDuration)}`
          : 'no suite data'}
        {summary.suiteCount > 0 && (
          <> &middot; {summary.suiteCount} suite{summary.suiteCount !== 1 ? 's' : ''}</>
        )}
        {summary.trend !== null && (
          <> &middot; <span className={`font-semibold ${trendColor}`}>{summary.trend > 0 ? '+' : ''}{summary.trend.toFixed(1)}%</span></>
        )}
      </div>
    </Link>
  );
}

function ChangeItem({ change: c }: { change: Change }) {
  const overThreshold = Math.abs(c.pctChange) >= (ALERT_RATIO - 1) * 100;
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
