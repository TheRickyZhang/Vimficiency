import { useState, useMemo, useEffect, useRef, useCallback } from 'react';
import { useNavigate, useParams } from '@tanstack/react-router';
import type { ExplorationData, ChunkedStateEntry, ChunkedResultEntry, ExploredStateEntry, FoundResultEntry } from '../types/exploration';
import { BufferPreview } from './BufferPreview';
import { EffortHistogram } from './EffortHistogram';
import { ExplorationTimeline } from './ExplorationTimeline';
import { ExplorationTree } from './ExplorationTree';
import {
  SUPERSCRIPTS,
  isChunkedCase,
  chunkDisplayKey,
  chunkedStatesToFlat,
  chunkedResultsToFlat,
  findCase,
  parseCaseName,
  deriveChunkDetail,
} from '../utils/exploration';

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

  const chunked = activeCase ? isChunkedCase(activeCase) : false;
  const hasStates = activeCase ? activeCase.states.length > 0 : false;
  const isLatestCommit = commitIdx === data.entries.length - 1;

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

  // For chunked cases, transform data; for flat cases, use directly
  const { flatStates, sortedResults } = useMemo(() => {
    if (!activeCase) return { flatStates: [] as ExploredStateEntry[], sortedResults: [] as FoundResultEntry[] };

    if (chunked) {
      const cStates = activeCase.states as unknown as ChunkedStateEntry[];
      const cResults = activeCase.results as unknown as ChunkedResultEntry[];
      const fs = chunkedStatesToFlat(cStates);
      const fr = chunkedResultsToFlat(cResults);
      const sorted = [...fr].sort((a, b) => a.effort - b.effort);
      return { flatStates: fs, sortedResults: sorted };
    }

    const sorted = [...(activeCase.results ?? [])].sort((a, b) => a.effort - b.effort);
    return { flatStates: activeCase.states, sortedResults: sorted };
  }, [activeCase, chunked]);

  // Multi-select: set of selected result sequences
  const [selectedSeqs, setSelectedSeqs] = useState<Set<string>>(() => new Set());

  // Side panel for chunk detail
  const [selectedChunk, setSelectedChunk] = useState<{ chunkIndex: number; chunkText: string; type: 'motion' | 'edit' } | null>(null);

  // Reset selection to best result when case or commit changes
  const activeCaseKey = `${commitIdx}:${activeCase?.name}`;
  useEffect(() => {
    if (sortedResults.length > 0) {
      setSelectedSeqs(new Set([sortedResults[0]!.tokens.join('')]));
    } else {
      setSelectedSeqs(new Set());
    }
    setSelectedChunk(null);
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

  // Build sub-tree data for the side panel (motion or edit chunk detail)
  const detailTreeData = useMemo(() => {
    if (!selectedChunk || !chunked || !activeCase) return null;
    return deriveChunkDetail(activeCase, selectedChunk.chunkIndex, selectedChunk.type);
  }, [selectedChunk, chunked, activeCase]);

  // Commit step
  const stepCommit = (delta: number) => {
    setCommitIdx((prev) => {
      const next = prev + delta;
      if (next < 0 || next >= data.entries.length) return prev;
      return next;
    });
    setSelectedCaseName(null);
  };

  // Expandable token indices: motion chunks + edit chunks with editStates
  const expandableTokens = useMemo(() => {
    if (!chunked || !activeCase) return undefined;
    const cResults = activeCase.results as unknown as ChunkedResultEntry[];
    if (!cResults.length) return undefined;
    const set = new Set<number>();
    const r = cResults[0]!;
    for (let i = 0; i < r.chunks.length; i++) {
      const ch = r.chunks[i]!;
      if (ch.type === 'motion') {
        set.add(i);
      } else if (ch.type === 'edit' && activeCase.diffs) {
        // Count which diff index this edit chunk corresponds to
        let editIdx = 0;
        for (let j = 0; j < i; j++) {
          if (r.chunks[j]!.type === 'edit') editIdx++;
        }
        const diff = activeCase.diffs[editIdx];
        if (diff?.editStates && diff.editStates.length > 0) {
          set.add(i);
        }
      }
    }
    return set.size > 0 ? set : undefined;
  }, [chunked, activeCase]);

  // Navigate to chunk detail page
  const navigate = useNavigate();
  const { optimizer } = useParams({ strict: false });
  const onChunkDetailNewPage = useCallback((tokenIndex: number, _tokenText: string) => {
    if (!activeCase) return;
    navigate({
      to: '/$optimizer/explore/chunk',
      params: { optimizer: optimizer! },
      search: { case: activeCase.name, chunkIndex: tokenIndex },
    });
  }, [activeCase, navigate, optimizer]);

  // Chunk detail callback for ExplorationTree
  const onChunkDetail = useCallback((tokenIndex: number, tokenText: string) => {
    if (!chunked || !activeCase) return;
    const cResults = activeCase.results as unknown as ChunkedResultEntry[];
    let type: 'motion' | 'edit' = 'motion';
    if (cResults.length > 0 && tokenIndex < cResults[0]!.chunks.length) {
      type = cResults[0]!.chunks[tokenIndex]!.type;
    }
    setSelectedChunk(prev =>
      prev?.chunkIndex === tokenIndex ? null : { chunkIndex: tokenIndex, chunkText: tokenText, type }
    );
  }, [chunked, activeCase]);

  return (
    <div>
      <h1>Search Space Explorer</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">A* explored states visualization</p>

      {/* Commit row */}
      <div className="mb-4 flex flex-col items-center">
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

      {/* Type / Size row */}
      <div className="flex gap-4 mb-6 flex-wrap items-end justify-center">
        <div>
          <label className="form-label">Type</label>
          <SearchableSelect
            value={activeCategory}
            options={categories.map((cat) => ({ value: cat, label: cat }))}
            onChange={setCategory}
          />
        </div>

        {paramsForCategory.length > 0 && (
          <div>
            <label className="form-label">Size</label>
            <SearchableSelect
              value={activeParam}
              options={paramsForCategory.map((p) => {
                const c = cases.find((c) => c.name === `${activeCategory}/${p}`);
                const suffix = c
                  ? c.states.length > 0
                    ? ` (${c.states.length} st)`
                    : ` (${c.nodesExplored} nodes)`
                  : '';
                return { value: p, label: `${p}${suffix}` };
              })}
              onChange={setParam}
            />
          </div>
        )}
      </div>

      {activeCase?.context && (
        <BufferPreview context={activeCase.context} diffs={activeCase.diffs} />
      )}

      {activeCase && (
        <div>
          {/* Found Results — multi-select toggles */}
          {sortedResults.length > 0 && (
            <Card title="Found Sequences" className="mb-6">
              <div className="flex flex-wrap gap-2 justify-center">
                {sortedResults.map((r) => {
                  const seq = r.tokens.join('');
                  const isSelected = selectedSeqs.has(seq);

                  // Color-coded rendering for chunked results
                  const renderTokens = () => {
                    if (!chunked) {
                      const display = seq.length > 30 ? seq.slice(0, 27) + '...' : seq;
                      return <code className="font-bold text-base" title={seq.length > 30 ? seq : undefined}>{display}</code>;
                    }
                    // Render chunk texts with motion=blue, edit=orange
                    const cResult = (activeCase!.results as unknown as ChunkedResultEntry[])
                      .find(cr => cr.chunks.map(chunkDisplayKey).join('') === seq);
                    if (!cResult) {
                      return <code className="font-bold text-base">{seq.length > 30 ? seq.slice(0, 27) + '...' : seq}</code>;
                    }
                    return (
                      <code className="font-bold text-base">
                        {cResult.chunks.map((ch, i) => (
                          <span key={i}>
                            {i > 0 && <span className="text-[#999]">{'\u00B7'}</span>}
                            <span className={ch.type === 'motion' ? 'text-[#1565c0]' : 'text-[#e65100]'}>
                              {chunkDisplayKey(ch)}
                            </span>
                          </span>
                        ))}
                      </code>
                    );
                  };

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
                      {renderTokens()}
                      <span className="text-[0.8rem] text-muted">
                        {r.effort.toFixed(2)}
                      </span>
                    </div>
                  );
                })}
              </div>
              {hasStates && (
                <div className="text-xs text-muted mt-1.5">
                  Click to toggle paths in tree (multi-select)
                </div>
              )}
              {/* Typed content legend (chunked mode) */}
              {chunked && activeCase.contents && activeCase.contents.length > 0 && (
                <div className="flex flex-wrap gap-3 mt-3 pt-3 border-t border-[#eee]">
                  {activeCase.contents.map((content, i) => (
                    <div key={i} className="flex items-baseline gap-1.5 bg-[#fff8e1] px-3 py-1 rounded border border-[#ffe082]">
                      <span className="font-bold text-[#e65100]">{SUPERSCRIPTS[i] ?? `[${i}]`}</span>
                      <code className="text-sm text-[#333] max-w-[300px] truncate" title={content}>
                        {content.length > 30 ? content.slice(0, 27) + '...' : content}
                      </code>
                    </div>
                  ))}
                </div>
              )}
            </Card>
          )}

          {hasStates ? (
            <>
              {/* Exploration Tree + optional side panel */}
              <Card title={`Exploration Tree (${activeCase.nodesExplored.toLocaleString()} nodes explored)`} className="mb-6">
                <div className={selectedChunk ? 'flex' : ''}>
                  <div className={selectedChunk ? 'flex-1 min-w-0' : ''}>
                    <ExplorationTree
                      states={flatStates}
                      results={sortedResults}
                      selectedSeqs={selectedSeqs}
                      expandableTokens={expandableTokens}
                      onChunkDetail={chunked ? onChunkDetail : undefined}
                      onChunkDetailNewPage={chunked ? onChunkDetailNewPage : undefined}
                    />
                  </div>
                  {selectedChunk && detailTreeData && (
                    <div className="flex-1 min-w-0 border-l border-[#e0e0e0]">
                      <div className="flex items-center justify-between mb-2 px-3 pt-1">
                        <h4 className="text-sm font-bold text-[#333]">
                          <span className={selectedChunk.type === 'motion' ? 'text-[#1565c0]' : 'text-[#e65100]'}>
                            {selectedChunk.type}
                          </span>
                          {' chunk: '}
                          <code>{selectedChunk.chunkText}</code>
                        </h4>
                        <button
                          onClick={() => setSelectedChunk(null)}
                          className="text-xs text-muted hover:text-[#333] px-1"
                        >
                          close
                        </button>
                      </div>
                      {detailTreeData.states.length > 0 ? (
                        <ExplorationTree
                          states={detailTreeData.states}
                          results={detailTreeData.results}
                          selectedSeqs={new Set()}
                        />
                      ) : (
                        <div className="text-sm text-muted py-4 text-center">
                          No detail states for this chunk
                        </div>
                      )}
                    </div>
                  )}
                </div>
              </Card>

              {/* Charts */}
              <div className="grid grid-cols-2 gap-6 mb-6">
                <ExpandableCard title="Effort Distribution">
                  <EffortHistogram states={flatStates} results={sortedResults} selectedSeq={primarySelectedSeq} />
                </ExpandableCard>
                <ExpandableCard title="Exploration Timeline">
                  <ExplorationTimeline states={flatStates} />
                </ExpandableCard>
              </div>
            </>
          ) : (
            <div className="card p-5 mb-6 text-center">
              <p className="text-sm text-muted">
                {activeCase.nodesExplored === 0
                  ? 'This case was not run for this commit.'
                  : !isLatestCommit
                    ? 'Exploration tree and charts are only available for the latest commit (older commits are stored in summary-only form).'
                    : 'Exploration tree and charts are unavailable: this build did not record per-state traces. Check that VIMF_TRACK_STATES was set when the explore binary was built.'}
              </p>
              <p className="text-xs text-muted mt-1">
                {activeCase.nodesExplored.toLocaleString()} nodes were explored.
              </p>
            </div>
          )}
        </div>
      )}
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
