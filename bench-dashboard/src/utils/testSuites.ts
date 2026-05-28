import type { BenchmarkData, BenchmarkRun } from '../types/benchmark';
import { loadBenchmarkData, toNanoseconds, timeSeries, type TimePoint } from './data';
import type { Nanoseconds } from './format';

export type TestSuiteKind = 'timing' | 'coverage';

export const TEST_SUITES = [
  { slug: 'unit', label: 'Unit', title: 'Unit Tests', kind: 'timing' },
  { slug: 'approval', label: 'Approval', title: 'Approval Tests', kind: 'timing' },
  { slug: 'property', label: 'Property', title: 'Property Tests', kind: 'coverage' },
  { slug: 'safety', label: 'Safety', title: 'Safety Tests', kind: 'coverage' },
] as const;

export type TestSuiteSlug = (typeof TEST_SUITES)[number]['slug'];

export interface TestSuiteSummary {
  slug: TestSuiteSlug;
  label: string;
  title: string;
  latestDuration: Nanoseconds | null;
  trend: number | null;
  suiteCount: number;
}

export interface ApprovalFixture {
  label: string;
  path: string;
  href?: string;
}

export function isTestSuiteSlug(value: string | undefined): value is TestSuiteSlug {
  return TEST_SUITES.some((suite) => suite.slug === value);
}

export function getTestSuite(slug: TestSuiteSlug) {
  return TEST_SUITES.find((suite) => suite.slug === slug)!;
}

export function testSuiteKind(slug: TestSuiteSlug): TestSuiteKind {
  return getTestSuite(slug).kind;
}

export function testSuiteDescription(slug: TestSuiteSlug) {
  switch (slug) {
    case 'unit':
      return 'Deterministic C++ gtest timing by test suite. Optimizer unit and regression tests live here; benchmark-only optimizer measurements stay on the optimizer pages.';
    case 'approval':
      return 'Approval snapshot timing by approval test file. Explore a file to see the approved fixtures matched by its cases.';
    case 'property':
      return 'FuzzTest generated-property coverage with the fixed CI seed. Shown as suites and cases, not timing: unit-mode wall-time is a fixed watchdog floor and carries no signal.';
    case 'safety':
      return 'FuzzTest safety and adversarial-input coverage with the fixed CI seed. Shown as suites and cases, not timing, for the same reason as the property suites.';
  }
}

export function binaryTotalName(slug: TestSuiteSlug): string {
  return `Tests/Binaries/${getTestSuite(slug).label}`;
}

export function binaryTotalSeries(raw: BenchmarkData, slug: TestSuiteSlug, repoUrl: string): TimePoint[] {
  return timeSeries(loadBenchmarkData(raw), binaryTotalName(slug), repoUrl);
}

export function loadTestSuiteBenchmarkData(raw: BenchmarkData, slug: TestSuiteSlug): BenchmarkRun[] {
  const suite = getTestSuite(slug);
  const casePrefix = `Tests/Cases/${suite.label}/`;
  return loadBenchmarkData(raw)
    .map((run) => ({
      ...run,
      benches: run.benches.flatMap((bench) =>
        bench.name.startsWith(casePrefix)
          ? [{ ...bench, name: mappedCaseName(suite.label, bench.name.slice(casePrefix.length)) }]
          : [],
      ),
    }))
    .filter((run) => run.benches.length > 0);
}

export function summarizeTestSuite(raw: BenchmarkData, slug: TestSuiteSlug): TestSuiteSummary | null {
  const suite = getTestSuite(slug);
  const runs = loadBenchmarkData(raw);
  const latest = runs[runs.length - 1];
  if (!latest) return null;

  const prev = runs.length > 1 ? runs[runs.length - 2] : undefined;
  const totalName = binaryTotalName(slug);
  const latestTotal = latest.benches.find((bench) => bench.name === totalName);
  const prevTotal = prev?.benches.find((bench) => bench.name === totalName);
  const latestDuration = latestTotal ? toNanoseconds(latestTotal.value, latestTotal.unit) : null;

  let trend: number | null = null;
  if (latestTotal && prevTotal) {
    const prevNs = toNanoseconds(prevTotal.value, prevTotal.unit);
    if (prevNs > 0) {
      trend = (toNanoseconds(latestTotal.value, latestTotal.unit) - prevNs) / prevNs * 100;
    }
  }

  const casePrefix = `Tests/Cases/${suite.label}/`;
  const suites = new Set<string>();
  for (const bench of latest.benches) {
    if (bench.name.startsWith(casePrefix)) {
      const rest = bench.name.slice(casePrefix.length);
      const dot = rest.indexOf('.');
      suites.add(dot < 0 ? rest : rest.slice(0, dot));
    }
  }

  return {
    slug,
    label: suite.label,
    title: suite.title,
    latestDuration,
    trend,
    suiteCount: suites.size,
  };
}

export function approvalFixturesForCategory(
  category: string,
  benchNames: string[],
  repoUrl: string,
  latestRun?: BenchmarkRun,
): ApprovalFixture[] {
  return benchNames.flatMap((benchName) => {
    const parsed = parseMappedTestBenchName(benchName);
    if (!parsed || parsed.label !== 'Approval' || parsed.group !== category) {
      return [];
    }
    const fixtureName = `${parsed.group}.${parsed.detail}`;
    const path = `tests/Approval/fixtures/${fixtureName}.approved.txt`;
    return [{
      label: fixtureName,
      path,
      href: githubBlobUrl(repoUrl, latestRun, path),
    }];
  });
}

function mappedCaseName(label: string, fullCaseName: string): string {
  const separator = fullCaseName.indexOf('.');
  if (separator < 0) return `${label}/${fullCaseName}/Case`;
  const suite = fullCaseName.slice(0, separator);
  const testCase = fullCaseName.slice(separator + 1);
  return `${label}/${suite}/${testCase}`;
}

function githubBlobUrl(repoUrl: string, latestRun: BenchmarkRun | undefined, path: string): string | undefined {
  if (!repoUrl || !latestRun?.commit.id) return undefined;
  return `${repoUrl}/blob/${latestRun.commit.id}/${path}`;
}

function parseMappedTestBenchName(name: string) {
  const [label, group, ...detailParts] = name.split('/');
  if (!label || !group || detailParts.length === 0) return null;
  return {
    label,
    group,
    detail: detailParts.join('/'),
  };
}
