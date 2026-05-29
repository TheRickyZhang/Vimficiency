import type {
  ExplorationData,
  ExplorationCase,
  ExploredStateEntry,
  FoundResultEntry,
  SequenceChunk,
  ChunkedStateEntry,
  ChunkedResultEntry,
  BufferContext,
  DiffRegion,
} from '../types/exploration';

export const SUPERSCRIPTS = ['\u00B9', '\u00B2', '\u00B3', '\u2074', '\u2075', '\u2076', '\u2077', '\u2078', '\u2079'];

export function isChunkedCase(c: ExplorationCase): boolean {
  return Array.isArray(c.contents);
}

export function chunkDisplayKey(c: SequenceChunk): string {
  if (c.contentId != null && c.contentId >= 0) return c.text + (SUPERSCRIPTS[c.contentId] ?? `[${c.contentId}]`);
  return c.text;
}

export function chunkedStatesToFlat(states: ChunkedStateEntry[]): ExploredStateEntry[] {
  return states.map(s => ({
    effort: s.effort,
    tokens: s.chunks.map(chunkDisplayKey),
  }));
}

export function chunkedResultsToFlat(results: ChunkedResultEntry[]): FoundResultEntry[] {
  return results.map(r => ({
    effort: r.effort,
    tokens: r.chunks.map(chunkDisplayKey),
  }));
}

export function findCase(cases: ExplorationCase[], query: string | null): ExplorationCase | undefined {
  if (!query) return cases[0];
  const exact = cases.find((c) => c.name === query);
  if (exact) return exact;
  // Exact param missing: land on the same category's first case if it exists,
  // otherwise the first case overall (caseCategoryExists tells the caller it was a miss).
  const queryCat = parseCaseName(query).category;
  const sameCat = cases.find((c) => parseCaseName(c.name).category === queryCat);
  return sameCat ?? cases[0];
}

export function parseCaseName(name: string): { category: string; param: string } {
  const slash = name.indexOf('/');
  if (slash === -1) return { category: name, param: '' };
  return { category: name.substring(0, slash), param: name.substring(slash + 1) };
}

/** Categories present in the latest exploration entry — used to gate Explore buttons. */
export function exploreCategoryNames(data: ExplorationData): string[] {
  const latest = data.entries[data.entries.length - 1];
  if (!latest) return [];
  const cats = new Set<string>();
  for (const c of latest.cases) cats.add(parseCaseName(c.name).category);
  return [...cats];
}

/** Whether any case shares the requested query's category. */
export function caseCategoryExists(cases: ExplorationCase[], query: string | null): boolean {
  if (!query) return true;
  const cat = parseCaseName(query).category;
  return cases.some((c) => parseCaseName(c.name).category === cat);
}

/** Derive sub-tree data for a chunk (edit or motion) from a chunked case */
export function deriveChunkDetail(
  activeCase: ExplorationCase,
  chunkIndex: number,
  chunkType: 'motion' | 'edit',
): { states: ExploredStateEntry[]; results: FoundResultEntry[] } | null {
  const cStates = activeCase.states as unknown as ChunkedStateEntry[];
  const cResults = activeCase.results as unknown as ChunkedResultEntry[];

  if (chunkType === 'edit') {
    let editIdx = 0;
    if (cResults.length > 0) {
      const r = cResults[0]!;
      for (let i = 0; i < chunkIndex && i < r.chunks.length; i++) {
        if (r.chunks[i]!.type === 'edit') editIdx++;
      }
    }
    const diff = activeCase.diffs?.[editIdx];
    if (!diff) return null;
    return {
      states: diff.editStates ?? [],
      results: diff.editResults ?? [],
    };
  }

  // Motion chunk: filter composition explored states by editsCompleted
  let targetEditsCompleted = 0;
  if (cResults.length > 0) {
    const r = cResults[0]!;
    for (let i = 0; i < chunkIndex && i < r.chunks.length; i++) {
      if (r.chunks[i]!.type === 'edit') targetEditsCompleted++;
    }
  }

  const subStates: ExploredStateEntry[] = [];
  for (const s of cStates) {
    if (s.editsCompleted !== targetEditsCompleted) continue;
    if (chunkIndex >= s.chunks.length) continue;
    const chunk = s.chunks[chunkIndex];
    if (!chunk || chunk.type !== 'motion') continue;
    subStates.push({ effort: s.effort, tokens: chunk.tokens });
  }

  const subResults: FoundResultEntry[] = [];
  for (const r of cResults) {
    if (chunkIndex >= r.chunks.length) continue;
    const chunk = r.chunks[chunkIndex]!;
    if (chunk.type !== 'motion') continue;
    subResults.push({ effort: r.effort, tokens: chunk.tokens });
  }

  return { states: subStates, results: subResults };
}

/** Derive a BufferContext for a chunk detail page */
export function deriveChunkBufferContext(
  parentContext: BufferContext | undefined,
  diffs: DiffRegion[] | undefined,
  chunkIndex: number,
  chunkType: 'motion' | 'edit',
  cResults: ChunkedResultEntry[],
): BufferContext | undefined {
  if (chunkType === 'motion') {
    // Motion chunks use the parent case's full buffer context
    return parentContext;
  }

  // Edit chunks: build mini buffer from the corresponding DiffRegion
  if (!diffs) return undefined;

  let editIdx = 0;
  if (cResults.length > 0) {
    const r = cResults[0]!;
    for (let i = 0; i < chunkIndex && i < r.chunks.length; i++) {
      if (r.chunks[i]!.type === 'edit') editIdx++;
    }
  }

  const diff = diffs[editIdx];
  if (!diff) return undefined;

  const initialLines = diff.deletedText ? diff.deletedText.split('\n') : [''];
  const goalLines = diff.insertedText ? diff.insertedText.split('\n') : [''];

  return { initialLines, goalLines };
}

/** Determine chunk type from results */
export function getChunkType(
  activeCase: ExplorationCase,
  chunkIndex: number,
): 'motion' | 'edit' {
  const cResults = activeCase.results as unknown as ChunkedResultEntry[];
  if (cResults.length > 0 && chunkIndex < cResults[0]!.chunks.length) {
    return cResults[0]!.chunks[chunkIndex]!.type;
  }
  return 'motion';
}

/** Get the text label for a chunk at a given index */
export function getChunkText(
  activeCase: ExplorationCase,
  chunkIndex: number,
): string {
  const cResults = activeCase.results as unknown as ChunkedResultEntry[];
  if (cResults.length > 0 && chunkIndex < cResults[0]!.chunks.length) {
    return chunkDisplayKey(cResults[0]!.chunks[chunkIndex]!);
  }
  return `chunk ${chunkIndex}`;
}
