// Loads the planner wasm module (built in CI into public/wasm/, see
// dev/web/how-it-works-site.md) and wraps its C exports. Each export returns a
// malloc'd JSON string; callJson frees both the inputs and the result.

export type Layout = 'qwerty' | 'colemak_dh' | 'uniform';

export type StepName =
  | 'LEADING' | 'MOVE' | 'CROSS' | 'DELETE' | 'CHANGE' | 'ENTER' | 'TYPE' | 'EXIT';

export interface Pos { line: number; col: number }

export interface Region {
  aBegin: number; aEnd: number; bBegin: number; bEnd: number;
  begin: Pos; end: Pos;
  deleted: string; inserted: string;
  del: number; ins: number; move: number;
}

export interface PlanResult { total: number; regions: Region[] }

export interface Cell { cost: number; step: StepName; pi: number; pj: number }

export interface GridBlock {
  aBegin: number; aEnd: number; bBegin: number; bEnd: number;
  lead: number; trail: number;
  out?: (Cell | null)[][];
  in?: (Cell | null)[][];
}

export interface PathCell { block: number; i: number; j: number; table: 'in' | 'out' }

export interface GridResult extends PlanResult {
  sealedFallback: boolean;
  dpCells: number; rawCells: number;
  blocks: GridBlock[];
  path: PathCell[];
}

// kind indexes CHAR, WORD, BIG_WORD, LINE, PARAGRAPH, LINE_END, LINE_START.
export interface Chunk { begin: number; end: number; kind: number; count: number; cost: number; label: string }

export interface TilingResult { cost: number; charOnly: number; chunks: Chunk[] }

export interface SealBlock { aBegin: number; aEnd: number; bBegin: number; bEnd: number }

export interface SealResult { N: number; M: number; dpCells: number; rawCells: number; blocks: SealBlock[] }

interface EmModule {
  cwrap(name: string, ret: 'number', args: 'number'[]): (...a: number[]) => number;
  stringToNewUTF8(s: string): number;
  UTF8ToString(p: number): string;
  _free(p: number): void;
}

let modulePromise: Promise<EmModule> | undefined;

export class PlannerUnavailable extends Error {}

export function loadPlanner(): Promise<EmModule> {
  if (!modulePromise) {
    const dir = `${import.meta.env.BASE_URL.replace(/\/$/, '')}/wasm/`;
    const url = `${dir}vimfy_planner.js`;
    // Fetched as text and imported from a blob: the dev server refuses to serve
    // a /public file as a module import, and the glue must stay unbundled.
    modulePromise = fetch(url)
      .then((r) => {
        if (!r.ok) throw new Error(`${r.status} ${r.statusText}`);
        return r.text();
      })
      .then((src) => import(/* @vite-ignore */ URL.createObjectURL(new Blob([src], { type: 'text/javascript' }))))
      .then((m) => m.default({ locateFile: (p: string) => dir + p }) as Promise<EmModule>)
      .catch((e) => {
        modulePromise = undefined;
        throw new PlannerUnavailable(`planner module failed to load from ${url}: ${e}`);
      });
  }
  return modulePromise;
}

type Arg = string | number;

async function callJson<T>(fn: string, ...args: Arg[]): Promise<T> {
  const M = await loadPlanner();
  const f = M.cwrap(fn, 'number', args.map(() => 'number'));
  const ptrs: number[] = [];
  const raw = args.map((a) => {
    if (typeof a === 'number') return a;
    const p = M.stringToNewUTF8(a);
    ptrs.push(p);
    return p;
  });
  const out = f(...raw);
  try {
    const parsed = JSON.parse(M.UTF8ToString(out));
    if (parsed && typeof parsed.error === 'string') throw new Error(parsed.error);
    return parsed as T;
  } finally {
    ptrs.forEach((p) => M._free(p));
    M._free(out);
  }
}

export interface Options { layout?: Layout; scale?: number; mxcnt?: number }

function opts(o: Options): [string, number, number] {
  return [o.layout ?? 'qwerty', o.scale ?? 0, o.mxcnt ?? 0];
}

export const plan = (initial: string, goal: string, o: Options = {}) =>
  callJson<PlanResult>('vf_plan', initial, goal, ...opts(o));

export const grid = (initial: string, goal: string, o: Options = {}) =>
  callJson<GridResult>('vf_grid', initial, goal, ...opts(o));

export const tiling = (initial: string, begin: number, end: number, kind: 'delete' | 'move', o: Options = {}) =>
  callJson<TilingResult>('vf_tiling', initial, begin, end, kind, o.scale ?? 0, o.mxcnt ?? 0);

export const seal = (initial: string, goal: string, o: Options = {}) =>
  callJson<SealResult>('vf_seal', initial, goal, ...opts(o));

// --- shared DOM helpers for the widgets ---------------------------------------

export function debounce<A extends unknown[]>(fn: (...a: A) => void, ms: number) {
  let t: ReturnType<typeof setTimeout> | undefined;
  return (...a: A) => {
    clearTimeout(t);
    t = setTimeout(() => fn(...a), ms);
  };
}

export function fmt(x: number): string {
  return Number.isInteger(x) ? String(x) : x.toFixed(2);
}

// Flat text as one run of fixed-width character cells; newlines show as ¶.
export function charCells(text: string, classAt: (index: number) => string | undefined): HTMLElement {
  const row = document.createElement('div');
  row.className = 'vf-chars';
  for (let r = 0; r < text.length; r++) {
    const span = document.createElement('span');
    const cls = classAt(r);
    span.className = 'vf-ch' + (cls ? ` ${cls}` : '');
    span.dataset.index = String(r);
    span.textContent = text[r] === '\n' ? '¶' : text[r];
    if (text[r] === '\n') span.classList.add('vf-nl');
    row.appendChild(span);
  }
  return row;
}

export function reportError(el: HTMLElement, e: unknown): void {
  el.textContent =
    e instanceof PlannerUnavailable
      ? 'The interactive planner is not available on this page build. The static example below still applies.'
      : `Planner error: ${e instanceof Error ? e.message : String(e)}`;
  el.classList.add('vf-error');
}
