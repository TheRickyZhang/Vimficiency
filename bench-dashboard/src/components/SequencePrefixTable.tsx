import type { ExploredStateEntry } from '../types/exploration';

interface Props {
  states: ExploredStateEntry[];
}

function getPrefix(seq: string, len: number): string {
  if (seq.length <= len) return seq || '(start)';
  return seq.substring(0, len);
}

export function SequencePrefixTable({ states }: Props) {
  if (!states.length) return null;

  // Group by first 1-char prefix
  const prefixCounts = new Map<string, { count: number; avgEffort: number; totalEffort: number }>();
  for (const s of states) {
    const prefix = getPrefix(s.seq, 1);
    const entry = prefixCounts.get(prefix);
    if (entry) {
      entry.count++;
      entry.totalEffort += s.effort;
      entry.avgEffort = entry.totalEffort / entry.count;
    } else {
      prefixCounts.set(prefix, { count: 1, avgEffort: s.effort, totalEffort: s.effort });
    }
  }

  const sorted = [...prefixCounts.entries()]
    .sort((a, b) => b[1].count - a[1].count)
    .slice(0, 15);

  return (
    <table style={{
      width: '100%',
      borderCollapse: 'collapse',
      fontSize: '0.9rem',
    }}>
      <thead>
        <tr style={{ borderBottom: '2px solid #e0e0e0', textAlign: 'left' }}>
          <th style={{ padding: '8px 12px', fontWeight: 700 }}>First move</th>
          <th style={{ padding: '8px 12px', fontWeight: 700, textAlign: 'right' }}>Count</th>
          <th style={{ padding: '8px 12px', fontWeight: 700, textAlign: 'right' }}>% of total</th>
          <th style={{ padding: '8px 12px', fontWeight: 700, textAlign: 'right' }}>Avg effort</th>
        </tr>
      </thead>
      <tbody>
        {sorted.map(([prefix, info]) => (
          <tr key={prefix} style={{ borderBottom: '1px solid #f0f0f0' }}>
            <td style={{ padding: '6px 12px', fontFamily: 'monospace', fontWeight: 600 }}>{prefix}</td>
            <td style={{ padding: '6px 12px', textAlign: 'right' }}>{info.count}</td>
            <td style={{ padding: '6px 12px', textAlign: 'right', color: '#666' }}>
              {((info.count / states.length) * 100).toFixed(1)}%
            </td>
            <td style={{ padding: '6px 12px', textAlign: 'right', color: '#666' }}>
              {info.avgEffort.toFixed(2)}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
