import { useState, useMemo } from 'react';
import type { ExplorationData, ExplorationCase } from '../types/exploration';
import { EffortHistogram } from './EffortHistogram';
import { ExplorationTimeline } from './ExplorationTimeline';
import { ExplorationTree } from './ExplorationTree';
import { SequencePrefixTable } from './SequencePrefixTable';

function getExplorationData(): ExplorationData | null {
  return window.EXPLORATION_DATA ?? null;
}

function findCase(cases: ExplorationCase[], query: string | null): ExplorationCase | undefined {
  if (!query) return cases[0];
  // Exact match
  const exact = cases.find((c) => c.name === query);
  if (exact) return exact;
  // Partial match (query is suffix of name or vice versa)
  const partial = cases.find((c) => c.name.endsWith(query) || query.endsWith(c.name));
  if (partial) return partial;
  return cases[0];
}

export function ExploreApp() {
  const data = useMemo(() => getExplorationData(), []);
  const params = useMemo(() => new URLSearchParams(location.search), []);
  const initialCase = params.get('case');

  const [commitIdx, setCommitIdx] = useState<number>(() => {
    return data ? data.entries.length - 1 : 0;
  });

  const entry = data?.entries[commitIdx];
  const cases = entry?.cases ?? [];

  // Resolve initial case, defaulting to first case
  const resolvedInitial = useMemo(() => findCase(cases, initialCase), [cases, initialCase]);
  const [selectedCaseName, setSelectedCaseName] = useState<string | null>(null);

  // Active case: use explicit selection if set, otherwise resolved initial
  const activeCase = selectedCaseName
    ? cases.find((c) => c.name === selectedCaseName) ?? resolvedInitial
    : resolvedInitial;

  if (!data || !data.entries.length) {
    return <p style={{ color: '#666', fontSize: '1.1rem' }}>No exploration data yet. Push to main to generate.</p>;
  }

  if (!entry) return null;

  return (
    <div>
      {/* Controls */}
      <div style={{ display: 'flex', gap: 16, marginBottom: 24, flexWrap: 'wrap' }}>
        <div>
          <label style={{ fontSize: '0.85rem', fontWeight: 600, color: '#555', display: 'block', marginBottom: 4 }}>
            Commit
          </label>
          <select
            value={commitIdx}
            onChange={(e) => { setCommitIdx(Number(e.target.value)); setSelectedCaseName(null); }}
            style={selectStyle}
          >
            {data.entries.map((e, i) => (
              <option key={e.commit.id} value={i}>
                {e.commit.id.substring(0, 7)} — {e.commit.message.split('\n')[0]?.substring(0, 40)}
              </option>
            ))}
          </select>
        </div>

        <div>
          <label style={{ fontSize: '0.85rem', fontWeight: 600, color: '#555', display: 'block', marginBottom: 4 }}>
            Case
          </label>
          <select
            value={activeCase?.name ?? ''}
            onChange={(e) => setSelectedCaseName(e.target.value)}
            style={selectStyle}
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
          {activeCase.results && activeCase.results.length > 0 && (
            <Card title="Found Sequences" style={{ marginBottom: 24 }}>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                {activeCase.results.map((r, i) => (
                  <div key={i} style={{
                    display: 'flex', alignItems: 'baseline', gap: 8,
                    padding: '6px 14px', background: i === 0 ? '#e8f5e9' : '#f5f5f5',
                    border: `1px solid ${i === 0 ? '#4caf50' : '#e0e0e0'}`,
                    borderRadius: 6,
                  }}>
                    <code style={{ fontWeight: 700, fontSize: '1rem' }}>{r.seq}</code>
                    <span style={{ fontSize: '0.8rem', color: '#888' }}>
                      {r.effort.toFixed(2)}
                    </span>
                  </div>
                ))}
              </div>
            </Card>
          )}

          {/* Stats summary */}
          <div style={{ display: 'flex', gap: 16, marginBottom: 24, flexWrap: 'wrap' }}>
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
          <Card title="Exploration Tree" style={{ marginBottom: 24 }}>
            <ExplorationTree states={activeCase.states} />
          </Card>

          {/* Charts */}
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 24, marginBottom: 24 }}>
            <Card title="Effort Distribution">
              <EffortHistogram states={activeCase.states} />
            </Card>
            <Card title="Exploration Timeline">
              <ExplorationTimeline states={activeCase.states} />
            </Card>
          </div>

          <Card title="Top First Moves">
            <SequencePrefixTable states={activeCase.states} />
          </Card>
        </div>
      )}
    </div>
  );
}

const selectStyle: React.CSSProperties = {
  padding: '6px 12px', fontSize: '0.9rem', borderRadius: 6,
  border: '1px solid #ccc', background: 'white',
};

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div style={{
      background: 'white', border: '1px solid #e0e0e0', borderRadius: 8,
      padding: '10px 16px', minWidth: 100,
    }}>
      <div style={{ fontSize: '0.75rem', color: '#888', fontWeight: 600, marginBottom: 2 }}>{label}</div>
      <div style={{ fontSize: '1.2rem', fontWeight: 800 }}>{value}</div>
    </div>
  );
}

function Card({ title, children, style }: { title: string; children: React.ReactNode; style?: React.CSSProperties }) {
  return (
    <div style={{
      background: 'white', border: '1px solid #e0e0e0', borderRadius: 8,
      padding: 20, ...style,
    }}>
      <h3 style={{ fontSize: '1rem', fontWeight: 700, marginBottom: 12, color: '#333' }}>{title}</h3>
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
