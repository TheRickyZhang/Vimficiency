import type { BenchmarkCommit } from './benchmark';

export interface ExploredStateEntry {
  effort: number;
  tokens: string[];
}

export interface FoundResultEntry {
  tokens: string[];
  effort: number;
}

// Chunked format (composition optimizer)
export interface SequenceChunk {
  type: 'motion' | 'edit';
  text: string;
  tokens: string[];
  contentId?: number;
}

export interface ChunkedStateEntry {
  effort: number;
  editsCompleted: number;
  chunks: SequenceChunk[];
}

export interface ChunkedResultEntry {
  effort: number;
  chunks: SequenceChunk[];
}

// Character-level diff region (composition optimizer)
export interface DiffRegion {
  beginLine: number;
  beginCol: number;
  endLine: number;
  endCol: number;
  deletedText: string;
  insertedText: string;
  // Per-diff edit exploration data (when trackExploredStates=true)
  editStates?: ExploredStateEntry[];
  editResults?: FoundResultEntry[];
}

export interface BufferContext {
  initialLines: string[];
  goalLines: string[];
  initialCursor?: [number, number];
  goalCursor?: [number, number];
  prefix?: string;
  suffix?: string;
  hasLinesAbove?: boolean;
  hasLinesBelow?: boolean;
}

export interface ExplorationCase {
  name: string;
  nodesExplored: number;
  context?: BufferContext;
  // Flat format (motion/edit)
  results: FoundResultEntry[];
  states: ExploredStateEntry[];
  // Chunked format (composition) — presence of contents indicates chunked mode
  contents?: string[];
  // Character-level diffs (composition optimizer)
  diffs?: DiffRegion[];
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