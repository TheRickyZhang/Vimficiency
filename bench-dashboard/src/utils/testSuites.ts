import type { BenchmarkData, BenchmarkRun } from '../types/benchmark';
import { loadBenchmarkData, toNanoseconds } from './data';
import { ns, type Nanoseconds } from './format';

export const TEST_SUITES = [
  { slug: 'unit', label: 'Unit', title: 'Unit Tests' },
  { slug: 'approval', label: 'Approval', title: 'Approval Tests' },
  { slug: 'property', label: 'Property', title: 'Property Tests' },
  { slug: 'safety', label: 'Safety', title: 'Safety Tests' },
] as const;

export type TestSuiteSlug = (typeof TEST_SUITES)[number]['slug'];

export interface TestSuiteSummary {
  slug: TestSuiteSlug;
  label: string;
  title: string;
  latestDuration: Nanoseconds | null;
  avgSuiteDuration: Nanoseconds | null;
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

export function testSuiteDescription(slug: TestSuiteSlug) {
  switch (slug) {
    case 'unit':
      return 'Deterministic C++ gtest timing by test suite. Optimizer unit and regression tests live here; benchmark-only optimizer measurements stay on the optimizer pages.';
    case 'approval':
      return 'Approval snapshot timing by approval test file. Explore a file to see the approved fixtures matched by its cases.';
    case 'property':
      return 'FuzzTest generated-property timing with the fixed CI seed. Unit mode is time-capped, so exact generated-input counts are not exported yet.';
    case 'safety':
      return 'FuzzTest safety and adversarial-input timing with the fixed CI seed. Unit mode is time-capped, so exact generated-input counts are not exported yet.';
  }
}

export function loadTestSuiteBenchmarkData(raw: BenchmarkData, slug: TestSuiteSlug): BenchmarkRun[] {
  const suite = getTestSuite(slug);
  return loadBenchmarkData(raw)
    .map((run) => {
      const legacyUnitRun = !run.benches.some((bench) => bench.name.startsWith('Tests/Binaries/'));
      const benches = run.benches.flatMap((bench) => {
        const name = mappedTestBenchName(bench.name, suite.label, legacyUnitRun);
        return name ? [{ ...bench, name }] : [];
      });
      return { ...run, benches };
    })
    .filter((run) => run.benches.length > 0);
}

export function summarizeTestSuite(raw: BenchmarkData, slug: TestSuiteSlug): TestSuiteSummary | null {
  const suite = getTestSuite(slug);
  const runs = loadBenchmarkData(raw);
  const latest = runs[runs.length - 1];
  if (!latest) return null;

  const prev = runs.length > 1 ? runs[runs.length - 2] : undefined;
  const totalName = `Tests/Binaries/${suite.label}`;
  const legacyTotalName = 'Tests/Total/All';
  const latestTotal = latest.benches.find((bench) => bench.name === totalName);
  const latestLegacyTotal = slug === 'unit'
    ? latest.benches.find((bench) => bench.name === legacyTotalName)
    : undefined;
  const totalBench = latestTotal ?? latestLegacyTotal;
  const prevTotal = prev?.benches.find((bench) => bench.name === totalName)
    ?? (slug === 'unit' ? prev?.benches.find((bench) => bench.name === legacyTotalName) : undefined);
  const latestDuration = totalBench ? toNanoseconds(totalBench.value, totalBench.unit) : null;

  let trend: number | null = null;
  if (totalBench && prevTotal) {
    const prevNs = toNanoseconds(prevTotal.value, prevTotal.unit);
    if (prevNs > 0) {
      trend = (toNanoseconds(totalBench.value, totalBench.unit) - prevNs) / prevNs * 100;
    }
  }

  const suitePrefix = `Tests/Suites/${suite.label}/`;
  const suiteBenches = latest.benches.filter((bench) => bench.name.startsWith(suitePrefix));
  const legacySuiteBenches = slug === 'unit'
    ? latest.benches.filter((bench) => bench.name.startsWith('Tests/Suites/'))
    : [];
  const allSuiteBenches = suiteBenches.length > 0 ? suiteBenches : legacySuiteBenches;
  const suiteTotal = allSuiteBenches.reduce(
    (total, bench) => total + toNanoseconds(bench.value, bench.unit),
    0,
  );

  return {
    slug,
    label: suite.label,
    title: suite.title,
    latestDuration,
    avgSuiteDuration: allSuiteBenches.length > 0 ? ns(suiteTotal / allSuiteBenches.length) : null,
    trend,
    suiteCount: allSuiteBenches.length,
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
    if (!parsed || parsed.label !== 'Approval' || parsed.group !== category || parsed.detail === 'Total') {
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

function mappedTestBenchName(name: string, label: string, legacyUnitRun: boolean): string | null {
  const suitePrefix = `Tests/Suites/${label}/`;
  if (name.startsWith(suitePrefix)) {
    return `${label}/${name.slice(suitePrefix.length)}/Total`;
  }

  const casePrefix = `Tests/Cases/${label}/`;
  if (name.startsWith(casePrefix)) {
    return mappedCaseName(label, name.slice(casePrefix.length));
  }

  if (!legacyUnitRun || label !== 'Unit') {
    return null;
  }

  if (name === 'Tests/Total/All') {
    return null;
  }

  return mappedLegacyUnitName(name);
}

function mappedCaseName(label: string, fullCaseName: string): string | null {
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

function mappedLegacyUnitName(name: string): string | null {
  const suitePrefix = 'Tests/Suites/';
  if (name.startsWith(suitePrefix)) {
    return `Unit/${name.slice(suitePrefix.length)}/Total`;
  }

  const casePrefix = 'Tests/Cases/';
  if (name.startsWith(casePrefix)) {
    return mappedCaseName('Unit', name.slice(casePrefix.length));
  }

  return null;
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
