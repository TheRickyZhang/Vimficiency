import type { BenchmarkData, BenchmarkRun } from '../types/benchmark';

export interface ParsedName {
  category: string;
  detail: string;
}

export interface TimePoint {
  sha: string;
  commitUrl: string;
  val: number;
  msg: string;
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
function toNanoseconds(value: number, unit: string): number {
  if (unit.startsWith('s')) return value * 1e9;
  if (unit.startsWith('ms')) return value * 1e6;
  if (unit.startsWith('us') || unit.startsWith('\u00b5s')) return value * 1e3;
  return value; // already ns
}

export function timeSeries(data: BenchmarkRun[], benchName: string, repoUrl: string, hiddenShas?: Set<string>): TimePoint[] {
  const series: TimePoint[] = [];
  for (const commit of data) {
    const sha = commit.commit.id.substring(0, 7);
    if (hiddenShas?.has(sha)) continue;
    const b = commit.benches.find((x) => x.name === benchName);
    if (b) {
      series.push({
        sha,
        commitUrl: commit.commit.url || `${repoUrl}/commit/${commit.commit.id}`,
        val: toNanoseconds(b.value, b.unit),
        msg: commit.commit.message.split('\n')[0] ?? '',
      });
    }
  }
  return series;
}
