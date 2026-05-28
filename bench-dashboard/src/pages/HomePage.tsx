import { Link } from '@tanstack/react-router';
import { homeRoute, type OptimizerSlug } from '../router';
import { parseName, toNanoseconds, loadBenchmarkData } from '../utils/data';
import { fmtTime } from '../utils/format';
import { TEST_SUITES, summarizeTestSuite, binaryTotalName, type TestSuiteSummary } from '../utils/testSuites';
import { MultiLineChart, type ChartLine } from '../components/MultiLineChart';
import type { CoverageBinary } from '../types/coverage';

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

const TEST_LINES: ChartLine[] = [
  { label: 'Unit', benchName: binaryTotalName('unit'), color: '#4285f4' },
  { label: 'Approval', benchName: binaryTotalName('approval'), color: '#9c27b0' },
];

export function HomePage() {
  const { optimizers, coverage } = homeRoute.useLoaderData();

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
  const testRuns = testOptimizer ? loadBenchmarkData(testOptimizer.data) : [];
  const hasTests = testRuns.length > 0 || coverage !== null;

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

      {hasTests && (
        <div className="mt-10">
          <h2>Tests</h2>
          <div className="grid grid-cols-[repeat(auto-fit,minmax(220px,1fr))] gap-4">
            {TEST_SUITES.map((suite) =>
              suite.kind === 'timing' ? (
                <TestSuiteCard
                  key={suite.slug}
                  summary={testOptimizer ? summarizeTestSuite(testOptimizer.data, suite.slug) : null}
                  slug={suite.slug}
                  title={suite.title}
                />
              ) : (
                <CoverageCard
                  key={suite.slug}
                  slug={suite.slug}
                  title={suite.title}
                  binary={coverage?.binaries[suite.label] ?? null}
                />
              ),
            )}
          </div>
        </div>
      )}

      {testRuns.length > 0 && (
        <div className="mt-10 max-w-[60%] mx-auto max-md:max-w-full">
          <h2>C++ Test Duration (Unit + Approval)</h2>
          <div style={{ height: 250 }}>
            <MultiLineChart runs={testRuns} lines={TEST_LINES} />
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

function TestSuiteCard({ summary, slug, title }: { summary: TestSuiteSummary | null; slug: string; title: string }) {
  const trend = summary?.trend ?? null;
  const trendColor = trend !== null
    ? (trend > 5 ? 'text-bad' : trend < -5 ? 'text-good' : 'text-muted')
    : undefined;

  return (
    <Link
      to="/$optimizer/$testSuite"
      params={{ optimizer: 'tests', testSuite: slug }}
      search={{ cat: undefined, bench: undefined }}
      className="block card card-hover p-5 no-underline text-inherit"
    >
      <div className="font-bold text-[1.1rem] mb-2">{title}</div>
      <div className="text-2xl font-extrabold text-[#333]">
        {summary?.latestDuration != null ? fmtTime(summary.latestDuration) : 'No data'}
      </div>
      <div className="text-[0.9rem] text-muted mt-1">
        {summary && summary.suiteCount > 0
          ? <>{summary.suiteCount} suite{summary.suiteCount !== 1 ? 's' : ''}</>
          : 'no suite data'}
        {trend !== null && (
          <> &middot; <span className={`font-semibold ${trendColor}`}>{trend > 0 ? '+' : ''}{trend.toFixed(1)}%</span></>
        )}
      </div>
    </Link>
  );
}

function CoverageCard({ binary, slug, title }: { binary: CoverageBinary | null; slug: string; title: string }) {
  return (
    <Link
      to="/$optimizer/$testSuite"
      params={{ optimizer: 'tests', testSuite: slug }}
      search={{ cat: undefined, bench: undefined }}
      className="block card card-hover p-5 no-underline text-inherit"
    >
      <div className="font-bold text-[1.1rem] mb-2">{title}</div>
      <div className="text-2xl font-extrabold text-[#333]">
        {binary ? `${binary.totalCases} case${binary.totalCases !== 1 ? 's' : ''}` : 'No data'}
      </div>
      <div className="text-[0.9rem] text-muted mt-1">
        {binary ? (
          <>
            {binary.suites.length} suite{binary.suites.length !== 1 ? 's' : ''} &middot;{' '}
            <span className={`font-semibold ${binary.failures === 0 ? 'text-good' : 'text-bad'}`}>
              {binary.failures === 0 ? 'all passing' : `${binary.failures} failing`}
            </span>
          </>
        ) : 'coverage only'}
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
            {'⚠'}
          </span>
        )}
        <span className={`font-bold text-[0.9rem] tabular-nums ${c.pctChange > 0 ? 'text-bad' : 'text-good'}`}>
          {sign}{c.pctChange.toFixed(1)}%
        </span>
      </div>
    </Link>
  );
}
