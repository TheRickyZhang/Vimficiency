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

/** Extract the benchmark run array from parsed data. Called once at startup. */
export function loadBenchmarkData(raw: BenchmarkData): BenchmarkRun[] {
  return Object.values(raw.entries)[0]!;
}

export function parseName(fullName: string): ParsedName {
  const parts = fullName.split('/').filter((s) => !s.startsWith('iterations:'));
  return {
    category: parts[1] ?? 'Default',
    detail: parts.length > 2 ? parts.slice(2).join(' / ') : parts[0] ?? fullName,
  };
}

export function discoverCategories(data: BenchmarkRun[]): Record<string, string[]> {
  const latest = data[data.length - 1]!;
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

export function timeSeries(data: BenchmarkRun[], benchName: string, repoUrl: string): TimePoint[] {
  const series: TimePoint[] = [];
  for (const commit of data) {
    const sha = commit.commit.id.substring(0, 7);
    const b = commit.benches.find((x) => x.name === benchName);
    if (b) {
      series.push({
        sha,
        commitUrl: (commit.commit.url && commit.commit.url !== '#') ? commit.commit.url : `${repoUrl}/commit/${commit.commit.id}`,
        val: toNanoseconds(b.value, b.unit),
        msg: commit.commit.message.split('\n')[0] ?? '',
        cpuTime: b.cpuTime != null ? toNanoseconds(b.cpuTime, b.unit) : undefined,
      });
    }
  }
  return series;
}

/** Compute relative time series normalized to first data point = 1.0. */
export function computeRelativeSeries(points: TimePoint[]): (number | null)[] {
  if (points.length === 0) return [];
  const base = points[0]!.val;
  if (base === 0) return points.map(() => null);
  return points.map((p) => p.val / base);
}
