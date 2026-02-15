import { useState, useMemo, useEffect, useRef, useCallback } from 'react';
import type { ExplorationData, ExplorationCase } from '../types/exploration';
import { EffortHistogram } from './EffortHistogram';
import { ExplorationTimeline } from './ExplorationTimeline';
import { ExplorationTree } from './ExplorationTree';

function findCase(cases: ExplorationCase[], query: string | null): ExplorationCase | undefined {
  if (!query) return cases[0];
  const exact = cases.find((c) => c.name === query);
  if (exact) return exact;
  const partial = cases.find((c) => c.name.endsWith(query) || query.endsWith(c.name));
  if (partial) return partial;
  return cases[0];
}

// Split case names like "BufferSize/10" into { category: "BufferSize", param: "10" }
function parseCaseName(name: string): { category: string; param: string } {
  const slash = name.indexOf('/');
  if (slash === -1) return { category: name, param: '' };
  return { category: name.substring(0, slash), param: name.substring(slash + 1) };
}

interface Props {
  data: ExplorationData;
  initialCase: string | null;
}

export function ExploreApp({ data, initialCase }: Props) {
  const [commitIdx, setCommitIdx] = useState<number>(data.entries.length - 1);

  const entry = data.entries[commitIdx]!;
  const cases = entry.cases;

  // Resolve initial case, defaulting to first case
  const resolvedInitial = useMemo(() => findCase(cases, initialCase), [cases, initialCase]);
  const [selectedCaseName, setSelectedCaseName] = useState<string | null>(null);

  // Active case: use explicit selection if set, otherwise resolved initial
  const activeCase = selectedCaseName
    ? cases.find((c) => c.name === selectedCaseName) ?? resolvedInitial
    : resolvedInitial;

  // Parse cases into categories and params
  const { categories, paramsByCategory } = useMemo(() => {
    const cats = new Map<string, string[]>();
    for (const c of cases) {
      const { category, param } = parseCaseName(c.name);
      if (!cats.has(category)) cats.set(category, []);
      cats.get(category)!.push(param);
    }
    return { categories: [...cats.keys()], paramsByCategory: cats };
  }, [cases]);

  const activeCategory = activeCase ? parseCaseName(activeCase.name).category : categories[0] ?? '';
  const activeParam = activeCase ? parseCaseName(activeCase.name).param : '';
  const paramsForCategory = paramsByCategory.get(activeCategory) ?? [];

  const setCategory = (cat: string) => {
    const params = paramsByCategory.get(cat);
    if (params && params.length > 0) setSelectedCaseName(`${cat}/${params[0]}`);
  };

  const setParam = (param: string) => {
    setSelectedCaseName(`${activeCategory}/${param}`);
  };

  // Sort results by increasing effort
  const sortedResults = useMemo(() =>
    [...(activeCase?.results ?? [])].sort((a, b) => a.effort - b.effort),
    [activeCase?.results]
  );

  // Multi-select: set of selected result sequences
  const [selectedSeqs, setSelectedSeqs] = useState<Set<string>>(() => new Set());

  // Reset selection to best result when case or commit changes
  const activeCaseKey = `${commitIdx}:${activeCase?.name}`;
  useEffect(() => {
    if (sortedResults.length > 0) {
      setSelectedSeqs(new Set([sortedResults[0]!.tokens.join('')]));
    } else {
      setSelectedSeqs(new Set());
    }
  }, [activeCaseKey]); // eslint-disable-line react-hooks/exhaustive-deps

  const toggleSeq = (seq: string) => {
    setSelectedSeqs((prev) => {
      const next = new Set(prev);
      if (next.has(seq)) next.delete(seq);
      else next.add(seq);
      return next;
    });
  };

  // For EffortHistogram: pass first selected or null
  const primarySelectedSeq = selectedSeqs.size > 0 ? [...selectedSeqs][0]! : null;

  // Commit step
  const stepCommit = (delta: number) => {
    setCommitIdx((prev) => {
      const next = prev + delta;
      if (next < 0 || next >= data.entries.length) return prev;
      return next;
    });
    setSelectedCaseName(null);
  };

  return (
    <div>
      <h1>Search Space Explorer</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">A* explored states visualization</p>

      {/* Controls */}
      <div className="flex gap-4 mb-6 flex-wrap items-end">
        {/* Commit with left/right arrows */}
        <div>
          <label className="form-label">Commit</label>
          <div className="flex items-center gap-1">
            <button
              onClick={() => stepCommit(-1)}
              disabled={commitIdx <= 0}
              className="px-2 py-1.5 rounded-md border border-[#ccc] bg-surface text-sm cursor-pointer disabled:opacity-30 disabled:cursor-default hover:bg-[#f0f0f0]"
              title="Previous commit"
            >&larr;</button>
            <SearchableSelect
              value={String(commitIdx)}
              options={data.entries.map((e, i) => ({
                value: String(i),
                label: `${e.commit.id.substring(0, 7)} — ${e.commit.message.split('\n')[0]?.substring(0, 40)}`,
              }))}
              onChange={(v) => { setCommitIdx(Number(v)); setSelectedCaseName(null); }}
            />
            <button
              onClick={() => stepCommit(1)}
              disabled={commitIdx >= data.entries.length - 1}
              className="px-2 py-1.5 rounded-md border border-[#ccc] bg-surface text-sm cursor-pointer disabled:opacity-30 disabled:cursor-default hover:bg-[#f0f0f0]"
              title="Next commit"
            >&rarr;</button>
          </div>
        </div>

        {/* Category */}
        <div>
          <label className="form-label">Type</label>
          <SearchableSelect
            value={activeCategory}
            options={categories.map((cat) => ({ value: cat, label: cat }))}
            onChange={setCategory}
          />
        </div>

        {/* Param */}
        {paramsForCategory.length > 0 && (
          <div>
            <label className="form-label">Size</label>
            <SearchableSelect
              value={activeParam}
              options={paramsForCategory.map((p) => {
                const c = cases.find((c) => c.name === `${activeCategory}/${p}`);
                return { value: p, label: `${p}${c ? ` (${c.states.length} st)` : ''}` };
              })}
              onChange={setParam}
            />
          </div>
        )}
      </div>

      {activeCase && (
        <div>
          {/* Found Results — multi-select toggles */}
          {sortedResults.length > 0 && (
            <Card title="Found Sequences" className="mb-6">
              <div className="flex flex-wrap gap-2 justify-center">
                {sortedResults.map((r) => {
                  const seq = r.tokens.join('');
                  const isSelected = selectedSeqs.has(seq);
                  return (
                    <div
                      key={seq}
                      onClick={() => toggleSeq(seq)}
                      className={`flex items-baseline gap-2 px-3.5 py-1.5 rounded-md cursor-pointer transition-[border-color,background] duration-150 border-2 ${
                        isSelected
                          ? 'bg-[#e3f2fd] border-[#1976d2]'
                          : 'bg-[#f5f5f5] border-border'
                      }`}
                    >
                      <code className="font-bold text-base">{seq}</code>
                      <span className="text-[0.8rem] text-muted">
                        {r.effort.toFixed(2)}
                      </span>
                    </div>
                  );
                })}
              </div>
              <div className="text-xs text-muted mt-1.5">
                Click to toggle paths in tree (multi-select)
              </div>
            </Card>
          )}

          {/* Stats summary */}
          <div className="flex gap-4 mb-6 flex-wrap justify-center">
            <Stat label="Nodes explored" value={activeCase.nodesExplored.toLocaleString()} />
            <Stat label="States tracked" value={activeCase.states.length.toLocaleString()} />
            <Stat label="Max effort" value={
              activeCase.states.length > 0
                ? Math.max(...activeCase.states.map((s) => s.effort)).toFixed(2)
                : '—'
            } />
            <Stat label="Median effort" value={
              activeCase.states.length > 0
                ? median(activeCase.states.map((s) => s.effort)).toFixed(2)
                : '—'
            } />
          </div>

          {/* Exploration Tree */}
          <Card title="Exploration Tree" className="mb-6">
            <ExplorationTree
              states={activeCase.states}
              results={sortedResults}
              selectedSeqs={selectedSeqs}
            />
          </Card>

          {/* Charts */}
          <div className="grid grid-cols-2 gap-6 mb-6">
            <ExpandableCard title="Effort Distribution">
              <EffortHistogram states={activeCase.states} results={sortedResults} selectedSeq={primarySelectedSeq} />
            </ExpandableCard>
            <ExpandableCard title="Exploration Timeline">
              <ExplorationTimeline states={activeCase.states} />
            </ExpandableCard>
          </div>
        </div>
      )}
    </div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="card px-4 py-2.5 min-w-[100px]">
      <div className="text-xs text-muted font-semibold mb-0.5">{label}</div>
      <div className="text-[1.2rem] font-extrabold">{value}</div>
    </div>
  );
}

function Card({ title, children, className }: { title: string; children: React.ReactNode; className?: string }) {
  return (
    <div className={`card p-5 ${className ?? ''}`}>
      <h3 className="text-base font-bold mb-3 text-[#333]">{title}</h3>
      {children}
    </div>
  );
}

// Card with fullscreen expand via Fullscreen API
function ExpandableCard({ title, children }: { title: string; children: React.ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);
  const [isFs, setIsFs] = useState(false);

  useEffect(() => {
    const handler = () => setIsFs(document.fullscreenElement === ref.current);
    document.addEventListener('fullscreenchange', handler);
    return () => document.removeEventListener('fullscreenchange', handler);
  }, []);

  const toggle = useCallback(() => {
    if (document.fullscreenElement) document.exitFullscreen();
    else ref.current?.requestFullscreen();
  }, []);

  return (
    <div
      ref={ref}
      className="card p-5 relative"
      style={isFs ? { display: 'flex', flexDirection: 'column' } : undefined}
    >
      <h3 className="text-base font-bold mb-3 text-[#333]">{title}</h3>
      <div style={isFs ? { flex: 1, minHeight: 0 } : undefined}>
        {children}
      </div>
      <button
        onClick={toggle}
        className="fullscreen-btn"
        title={isFs ? 'Exit fullscreen' : 'Fullscreen'}
      >
        <svg viewBox="0 0 16 16" width="14" height="14">
          {isFs ? (
            <path d="M5 2v3H2M11 2v3h3M2 11h3v3M14 11h-3v3" fill="none" stroke="currentColor" strokeWidth="1.5" />
          ) : (
            <path d="M2 6V2h4M10 2h4v4M14 10v4h-4M6 14H2v-4" fill="none" stroke="currentColor" strokeWidth="1.5" />
          )}
        </svg>
      </button>
    </div>
  );
}

// Searchable dropdown (filterable options)
function SearchableSelect({ value, options, onChange }: {
  value: string;
  options: { value: string; label: string }[];
  onChange: (value: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const [filter, setFilter] = useState('');
  const ref = useRef<HTMLDivElement>(null);
  const inputRef = useRef<HTMLInputElement>(null);

  // Close on outside click
  useEffect(() => {
    if (!open) return;
    const handler = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener('mousedown', handler);
    return () => document.removeEventListener('mousedown', handler);
  }, [open]);

  // Focus input when opened
  useEffect(() => {
    if (open) {
      setFilter('');
      setTimeout(() => inputRef.current?.focus(), 0);
    }
  }, [open]);

  const current = options.find((o) => o.value === value);
  const filtered = filter
    ? options.filter((o) => o.label.toLowerCase().includes(filter.toLowerCase()))
    : options;

  return (
    <div ref={ref} className="relative">
      <button
        onClick={() => setOpen(!open)}
        className="px-3 py-1.5 text-[0.9rem] rounded-md border border-[#ccc] bg-surface text-left min-w-[120px] cursor-pointer hover:bg-[#f8f8f8] flex items-center gap-1"
      >
        <span className="flex-1 truncate">{current?.label ?? value}</span>
        <span className="text-[0.7rem] text-muted">{open ? '\u25B2' : '\u25BC'}</span>
      </button>
      {open && (
        <div className="absolute z-20 top-full mt-1 left-0 min-w-full bg-white border border-[#ccc] rounded-md shadow-lg max-h-[300px] flex flex-col">
          {options.length > 5 && (
            <input
              ref={inputRef}
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
              placeholder="Search..."
              className="px-2 py-1.5 text-sm border-b border-[#eee] outline-none"
            />
          )}
          <div className="overflow-y-auto">
            {filtered.map((o) => (
              <div
                key={o.value}
                onClick={() => { onChange(o.value); setOpen(false); }}
                className={`px-3 py-1.5 text-[0.9rem] cursor-pointer hover:bg-[#f0f4ff] ${
                  o.value === value ? 'bg-[#e3f2fd] font-semibold' : ''
                }`}
              >
                {o.label}
              </div>
            ))}
            {filtered.length === 0 && (
              <div className="px-3 py-2 text-sm text-muted">No matches</div>
            )}
          </div>
        </div>
      )}
    </div>
  );
}

function median(values: number[]): number {
  if (!values.length) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[mid]! : (sorted[mid - 1]! + sorted[mid]!) / 2;
}
