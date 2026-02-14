import type { BenchmarkCommit } from './benchmark';

export interface ExploredStateEntry {
  effort: number;
  tokens: string[];
}

export interface FoundResultEntry {
  tokens: string[];
  effort: number;
}

export interface ExplorationCase {
  name: string;
  nodesExplored: number;
  results: FoundResultEntry[];
  states: ExploredStateEntry[];
}

export interface ExplorationCommitEntry {
  commit: BenchmarkCommit;
  date: number;
  cases: ExplorationCase[];
}

export interface ExplorationData {
  lastUpdate: number;
  entries: ExplorationCommitEntry[];
}