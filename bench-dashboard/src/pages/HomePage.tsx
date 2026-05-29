import { Fragment, type ReactNode } from 'react';
import { Link } from '@tanstack/react-router';
import { homeRoute, type OptimizerSlug } from '../router';
import { parseName, toNanoseconds, loadBenchmarkData, discoverCategories } from '../utils/data';
import { fmtTime, ns, type Nanoseconds } from '../utils/format';
import { TEST_SUITES, summarizeTestSuite, binaryTotalName } from '../utils/testSuites';
import { MultiLineChart, type ChartLine } from '../components/MultiLineChart';
import type { BenchmarkData } from '../types/benchmark';

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
const CARD_CLASS = 'block card card-hover p-5 no-underline text-inherit';

const OPTIMIZER_LABELS: Partial<Record<OptimizerSlug, string>> = {
  edit: 'TransformOptimizer',
  motion: 'NavOptimizer',
  composition: 'CompositionOptimizer',
};

const TEST_LINES: ChartLine[] = [
  { label: 'Unit', benchName: binaryTotalName('unit'), color: '#4285f4' },
  { label: 'Approval', benchName: binaryTotalName('approval'), color: '#9c27b0' },
];

// Summed wall-time of every bench in the latest commit, with the % change from
// the previous commit. Mirrors summarizeTestSuite for the optimizer cards.
function summarizeOptimizer(data: BenchmarkData): { total: Nanoseconds; trend: number | null; categories: number } | null {
  const runs = loadBenchmarkData(data);
  const latest = runs[runs.length - 1];
  if (!latest) return null;
  const prev = runs.length > 1 ? runs[runs.length - 2] : undefined;
  let lSum = 0, pSum = 0;
  for (const b of latest.benches) lSum += toNanoseconds(b.value, b.unit);
  if (prev) for (const b of prev.benches) pSum += toNanoseconds(b.value, b.unit);
  const trend = (prev && pSum > 0) ? (lSum - pSum) / pSum * 100 : null;
  return { total: ns(lSum), trend, categories: Object.keys(discoverCategories(runs)).length };
}

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
        {(['edit', 'motion', 'composition'] as const).map((slug) => {
          const optData = optimizers.find((o) => o.slug === slug)?.data;
          const sum = optData ? summarizeOptimizer(optData) : null;
          const segments: ReactNode[] = sum
            ? [
              `${sum.categories} categor${sum.categories !== 1 ? 'ies' : 'y'}`,
              sum.trend !== null ? <Trend pct={sum.trend} /> : null,
              fmtTime(sum.total),
            ]
            : ['No data'];
          return (
            <HomeCard
              key={slug}
              nav={{ kind: 'optimizer', slug }}
              title={OPTIMIZER_LABELS[slug] ?? slug}
              segments={segments}
            />
          );
        })}
      </div>

      {hasTests && (
        <div className="mt-10">
          <h2>Tests</h2>
          <div className="grid grid-cols-[repeat(auto-fit,minmax(220px,1fr))] gap-4">
            {TEST_SUITES.map((suite) => {
              if (suite.kind === 'timing') {
                const s = testOptimizer ? summarizeTestSuite(testOptimizer.data, suite.slug) : null;
                const segments: ReactNode[] = s
                  ? [
                    s.suiteCount > 0 ? `${s.suiteCount} suite${s.suiteCount !== 1 ? 's' : ''}` : null,
                    s.trend !== null ? <Trend pct={s.trend} /> : null,
                    s.latestDuration != null ? fmtTime(s.latestDuration) : null,
                  ]
                  : ['No data'];
                return (
                  <HomeCard key={suite.slug} nav={{ kind: 'suite', slug: suite.slug }} title={suite.title} segments={segments} />
                );
              }
              const binary = coverage?.binaries[suite.label] ?? null;
              const segments: ReactNode[] = binary
                ? [
                  `${binary.suites.length} suite${binary.suites.length !== 1 ? 's' : ''}`,
                  `${binary.totalCases} case${binary.totalCases !== 1 ? 's' : ''}`,
                  <Passing failures={binary.failures} />,
                ]
                : ['No data'];
              return (
                <HomeCard key={suite.slug} nav={{ kind: 'suite', slug: suite.slug }} title={suite.title} segments={segments} />
              );
            })}
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

type CardNav =
  | { kind: 'optimizer'; slug: OptimizerSlug }
  | { kind: 'suite'; slug: string };

function HomeCard({ nav, title, segments }: { nav: CardNav; title: string; segments: ReactNode[] }) {
  const body = (
    <>
      <div className="font-bold text-[1.4rem] text-[#333] mb-1">{title}</div>
      <MetaLine segments={segments} />
    </>
  );
  return nav.kind === 'optimizer' ? (
    <Link to="/$optimizer" params={{ optimizer: nav.slug }} search={{ cat: undefined, bench: undefined }} className={CARD_CLASS}>
      {body}
    </Link>
  ) : (
    <Link to="/$optimizer/$testSuite" params={{ optimizer: 'tests', testSuite: nav.slug }} search={{ cat: undefined, bench: undefined }} className={CARD_CLASS}>
      {body}
    </Link>
  );
}

function MetaLine({ segments }: { segments: ReactNode[] }) {
  const kept = segments.filter((s) => s !== null && s !== undefined && s !== '');
  return (
    <div className="text-[0.9rem] text-muted">
      {kept.map((s, i) => <Fragment key={i}>{i > 0 && ' · '}{s}</Fragment>)}
    </div>
  );
}

function Trend({ pct }: { pct: number }) {
  const color = pct > 5 ? 'text-bad' : pct < -5 ? 'text-good' : 'text-muted';
  return <span className={`font-semibold ${color}`}>{pct > 0 ? '+' : ''}{pct.toFixed(1)}%</span>;
}

function Passing({ failures }: { failures: number }) {
  return (
    <span className={`font-semibold ${failures === 0 ? 'text-good' : 'text-bad'}`}>
      {failures === 0 ? 'all passing' : `${failures} failing`}
    </span>
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
