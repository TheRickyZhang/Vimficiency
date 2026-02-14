import type { BenchmarkCommit } from './benchmark';

export interface ExploredStateEntry {
  effort: number;
  seq: string;
}

export interface ExplorationCase {
  name: string;
  nodesExplored: number;
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

declare global {
  interface Window {
    EXPLORATION_DATA?: ExplorationData;
  }
}
