import { useState, useMemo } from 'react';
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
  const [selectedResultSeq, setSelectedResultSeq] = useState<string | null>(null);

  // Active case: use explicit selection if set, otherwise resolved initial
  const activeCase = selectedCaseName
    ? cases.find((c) => c.name === selectedCaseName) ?? resolvedInitial
    : resolvedInitial;

  // Sort results by increasing effort
  const sortedResults = useMemo(() =>
    [...(activeCase?.results ?? [])].sort((a, b) => a.effort - b.effort),
    [activeCase?.results]
  );

  return (
    <div>
      <h1>Search Space Explorer</h1>
      <p className="text-[#666] text-[1.1rem] mb-10">A* explored states visualization</p>

      {/* Controls */}
      <div className="flex gap-4 mb-6 flex-wrap">
        <div>
          <label className="text-[0.85rem] font-semibold text-[#555] block mb-1">
            Commit
          </label>
          <select
            value={commitIdx}
            onChange={(e) => { setCommitIdx(Number(e.target.value)); setSelectedCaseName(null); setSelectedResultSeq(null); }}
            className="px-3 py-1.5 text-[0.9rem] rounded-md border border-[#ccc] bg-surface"
          >
            {data.entries.map((e, i) => (
              <option key={e.commit.id} value={i}>
                {e.commit.id.substring(0, 7)} — {e.commit.message.split('\n')[0]?.substring(0, 40)}
              </option>
            ))}
          </select>
        </div>

        <div>
          <label className="text-[0.85rem] font-semibold text-[#555] block mb-1">
            Case
          </label>
          <select
            value={activeCase?.name ?? ''}
            onChange={(e) => { setSelectedCaseName(e.target.value); setSelectedResultSeq(null); }}
            className="px-3 py-1.5 text-[0.9rem] rounded-md border border-[#ccc] bg-surface"
          >
            {cases.map((c) => (
              <option key={c.name} value={c.name}>
                {c.name} ({c.states.length} states)
              </option>
            ))}
          </select>
        </div>
      </div>

      {activeCase && (
        <div>
          {/* Found Results — most important, at the top */}
          {sortedResults.length > 0 && (
            <Card title="Found Sequences" className="mb-6">
              <div className="flex flex-wrap gap-2">
                {sortedResults.map((r) => {
                  const seq = r.tokens.join('');
                  const isSelected = selectedResultSeq === seq;
                  return (
                    <div
                      key={seq}
                      onClick={() => setSelectedResultSeq(isSelected ? null : seq)}
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
              {selectedResultSeq && (
                <div className="text-xs text-muted mt-1.5">
                  Click again to deselect
                </div>
              )}
            </Card>
          )}

          {/* Stats summary */}
          <div className="flex gap-4 mb-6 flex-wrap">
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
            <ExplorationTree states={activeCase.states} results={sortedResults} />
          </Card>

          {/* Charts */}
          <div className="grid grid-cols-2 gap-6 mb-6">
            <Card title="Effort Distribution">
              <EffortHistogram states={activeCase.states} results={sortedResults} selectedSeq={selectedResultSeq} />
            </Card>
            <Card title="Exploration Timeline">
              <ExplorationTimeline states={activeCase.states} />
            </Card>
          </div>
        </div>
      )}
    </div>
  );
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="bg-surface border border-border rounded-lg px-4 py-2.5 min-w-[100px]">
      <div className="text-xs text-muted font-semibold mb-0.5">{label}</div>
      <div className="text-[1.2rem] font-extrabold">{value}</div>
    </div>
  );
}

function Card({ title, children, className }: { title: string; children: React.ReactNode; className?: string }) {
  return (
    <div className={`bg-surface border border-border rounded-lg p-5 ${className ?? ''}`}>
      <h3 className="text-base font-bold mb-3 text-[#333]">{title}</h3>
      {children}
    </div>
  );
}

function median(values: number[]): number {
  if (!values.length) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 ? sorted[mid]! : (sorted[mid - 1]! + sorted[mid]!) / 2;
}
