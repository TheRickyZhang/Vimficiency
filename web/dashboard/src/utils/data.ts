import type { BenchmarkData, BenchmarkRun } from '../types/benchmark';
import type { Nanoseconds } from './format';

export interface ParsedName {
  category: string;
  detail: string;
}

export interface TimePoint {
  sha: string;
  commitUrl: string;
  val: Nanoseconds;
  msg: string;
  cpuTime?: Nanoseconds;
}

/** Extract the benchmark run array from parsed data. Called once at startup.
 *  Uses the suite with the most recent entry (handles rename from Benchmark -> EditOpt etc.). */
export function loadBenchmarkData(raw: BenchmarkData): BenchmarkRun[] {
  const suites = Object.values(raw.entries);
  if (suites.length <= 1) return suites[0] ?? [];
  return suites.reduce((best, suite) => {
    const bestDate = best[best.length - 1]?.date ?? 0;
    const suiteDate = suite[suite.length - 1]?.date ?? 0;
    return suiteDate > bestDate ? suite : best;
  });
}

export function parseName(fullName: string): ParsedName {
  const parts = fullName.split('/').filter((s) => !s.startsWith('iterations:'));
  return {
    category: parts[1] ?? 'Default',
    detail: parts.length > 2 ? parts.slice(2).join(' / ') : parts[0] ?? fullName,
  };
}

export function discoverCategories(data: BenchmarkRun[]): Record<string, string[]> {
  const latest = data[data.length - 1];
  if (!latest) return {};

  const cats: Record<string, string[]> = {};
  for (const b of latest.benches) {
    const cat = parseName(b.name).category;
    (cats[cat] ??= []).push(b.name);
  }
  return cats;
}

/** Convert a benchmark value to nanoseconds based on the stored unit string. */
export function toNanoseconds(value: number, unit: string): Nanoseconds {
  let ns: number;
  if (unit.startsWith('ms')) ns = value * 1e6;
  else if (unit.startsWith('us') || unit.startsWith('\u00b5s')) ns = value * 1e3;
  else if (unit.startsWith('s')) ns = value * 1e9;
  else ns = value; // already ns
  return ns as Nanoseconds;
}

function timePoint(commit: BenchmarkRun, val: number, repoUrl: string): TimePoint {
  return {
    sha: commit.commit.id.substring(0, 7),
    commitUrl: (commit.commit.url && commit.commit.url !== '#') ? commit.commit.url : `${repoUrl}/commit/${commit.commit.id}`,
    val: val as Nanoseconds,
    msg: commit.commit.message.split('\n')[0] ?? '',
  };
}

export function timeSeries(data: BenchmarkRun[], benchName: string, repoUrl: string): TimePoint[] {
  const series: TimePoint[] = [];
  for (const commit of data) {
    const b = commit.benches.find((x) => x.name === benchName);
    if (b) {
      const point = timePoint(commit, toNanoseconds(b.value, b.unit), repoUrl);
      if (b.cpuTime != null) point.cpuTime = toNanoseconds(b.cpuTime, b.unit);
      series.push(point);
    }
  }
  return series;
}

function median(nums: number[]): number {
  const sorted = [...nums].sort((a, b) => a - b);
  const mid = sorted.length >> 1;
  return sorted.length % 2 === 0 ? (sorted[mid - 1]! + sorted[mid]!) / 2 : sorted[mid]!;
}

/** Per commit, the median across all benches of each bench's time relative to its
 *  own first appearance (baseline = 1.0). Equal weight per bench, so a few slow
 *  benches don't dominate the way a raw sum does. The series starts at 1.0; the
 *  stored `val` is a unitless ratio, rendered via the chart's relative metric. */
export function medianRelativeSeries(data: BenchmarkRun[], repoUrl: string): TimePoint[] {
  const baselines = new Map<string, number>();
  const series: TimePoint[] = [];
  for (const commit of data) {
    const ratios: number[] = [];
    for (const b of commit.benches) {
      const val = toNanoseconds(b.value, b.unit);
      if (val === 0) continue;
      let base = baselines.get(b.name);
      if (base === undefined) { base = val; baselines.set(b.name, val); }
      ratios.push(val / base);
    }
    if (ratios.length > 0) series.push(timePoint(commit, median(ratios), repoUrl));
  }
  return series;
}

/** Per commit, the sum of all bench times. Skews toward the longest-running benches;
 *  offered as the toggle alternative to {@link medianRelativeSeries}. */
export function totalSeries(data: BenchmarkRun[], repoUrl: string): TimePoint[] {
  return data.map((commit) =>
    timePoint(commit, commit.benches.reduce((sum, b) => sum + toNanoseconds(b.value, b.unit), 0), repoUrl),
  );
}

/** Compute relative time series normalized to first data point = 1.0. */
export function computeRelativeSeries(points: TimePoint[]): (number | null)[] {
  if (points.length === 0) return [];
  const base = points[0]!.val;
  if (base === 0) return points.map(() => null);
  return points.map((p) => p.val / base);
}
