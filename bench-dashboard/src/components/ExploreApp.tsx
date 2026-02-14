import { useState, useMemo } from 'react';
import type { ExplorationData, ExplorationCase } from '../types/exploration';
import { EffortHistogram } from './EffortHistogram';
import { ExplorationTimeline } from './ExplorationTimeline';
import { SequencePrefixTable } from './SequencePrefixTable';

function getExplorationData(): ExplorationData | null {
  return window.EXPLORATION_DATA ?? null;
}

export function ExploreApp() {
  const data = useMemo(() => getExplorationData(), []);
  const params = useMemo(() => new URLSearchParams(location.search), []);
  const initialCase = params.get('case');

  const [commitIdx, setCommitIdx] = useState<number>(() => {
    return data ? data.entries.length - 1 : 0;
  });
  const [selectedCase, setSelectedCase] = useState<string | null>(initialCase);

  if (!data || !data.entries.length) {
    return <p style={{ color: '#666', fontSize: '1.1rem' }}>No exploration data yet. Push to main to generate.</p>;
  }

  const entry = data.entries[commitIdx];
  if (!entry) return null;

  const cases = entry.cases;
  const activeCase: ExplorationCase | undefined = selectedCase
    ? cases.find((c) => c.name === selectedCase)
    : cases[0];

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
            onChange={(e) => setCommitIdx(Number(e.target.value))}
            style={{
              padding: '6px 12px', fontSize: '0.9rem', borderRadius: 6,
              border: '1px solid #ccc', background: 'white',
            }}
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
            onChange={(e) => setSelectedCase(e.target.value)}
            style={{
              padding: '6px 12px', fontSize: '0.9rem', borderRadius: 6,
              border: '1px solid #ccc', background: 'white',
            }}
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
          {/* Stats summary */}
          <div style={{
            display: 'flex', gap: 24, marginBottom: 24, flexWrap: 'wrap',
          }}>
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

          {/* Visualizations */}
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 24, marginBottom: 32 }}>
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

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div style={{
      background: 'white', border: '1px solid #e0e0e0', borderRadius: 8,
      padding: '12px 20px', minWidth: 120,
    }}>
      <div style={{ fontSize: '0.8rem', color: '#888', fontWeight: 600, marginBottom: 2 }}>{label}</div>
      <div style={{ fontSize: '1.3rem', fontWeight: 800 }}>{value}</div>
    </div>
  );
}

function Card({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div style={{
      background: 'white', border: '1px solid #e0e0e0', borderRadius: 8,
      padding: 20,
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
