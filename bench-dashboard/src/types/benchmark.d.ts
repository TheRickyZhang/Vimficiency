export interface BenchmarkCommit {
  id: string;
  message: string;
  timestamp: string;
  url: string;
  author: { username: string };
}

export interface BenchmarkEntry {
  name: string;
  value: number;
  unit: string;
  range?: string;
  extra?: string;
  cpuTime?: number;
}

export interface BenchmarkRun {
  commit: BenchmarkCommit;
  date: number;
  benches: BenchmarkEntry[];
}

export interface BenchmarkData {
  lastUpdate: number;
  repoUrl: string;
  entries: Record<string, BenchmarkRun[]>;
}