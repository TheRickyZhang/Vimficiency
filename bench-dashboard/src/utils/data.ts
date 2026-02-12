import type { BenchmarkRun } from '../types/benchmark';

export interface ParsedName {
  category: string;
  detail: string;
}

export interface TimePoint {
  sha: string;
  val: number;
  msg: string;
}

export function getData(): BenchmarkRun[] {
  if (!window.BENCHMARK_DATA) return [];
  const entries = window.BENCHMARK_DATA.entries;
  const key = Object.keys(entries)[0];
  return key ? entries[key] ?? [] : [];
}

export function parseName(fullName: string): ParsedName {
  const parts = fullName.split('/').filter((s) => !s.startsWith('iterations:'));
  return {
    category: parts[1] ?? 'Default',
    detail: parts.length > 2 ? parts.slice(2).join(' / ') : parts[0] ?? fullName,
  };
}

export function discoverCategories(data: BenchmarkRun[]): Record<string, string[]> {
  if (!data.length) return {};
  const latest = data[data.length - 1]!;
  const cats: Record<string, string[]> = {};
  for (const b of latest.benches) {
    const cat = parseName(b.name).category;
    (cats[cat] ??= []).push(b.name);
  }
  return cats;
}

export function timeSeries(data: BenchmarkRun[], benchName: string, hiddenShas?: Set<string>): TimePoint[] {
  const series: TimePoint[] = [];
  for (const commit of data) {
    const sha = commit.commit.id.substring(0, 7);
    if (hiddenShas?.has(sha)) continue;
    const b = commit.benches.find((x) => x.name === benchName);
    if (b) {
      series.push({
        sha,
        val: b.value,
        msg: commit.commit.message.split('\n')[0] ?? '',
      });
    }
  }
  return series;
}
