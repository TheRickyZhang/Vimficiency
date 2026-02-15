import type { Metric } from './BenchmarkChart';

const METRICS: { key: Metric; label: string; color: string }[] = [
  { key: 'wallclock', label: 'Wall Clock', color: '#4285f4' },
  { key: 'instructions', label: 'Instructions', color: '#9c27b0' },
  { key: 'relative', label: 'Relative', color: '#34a853' },
];

interface Props {
  activeMetrics: Set<Metric>;
  onChange: (metrics: Set<Metric>) => void;
}

export function MetricToggles({ activeMetrics, onChange }: Props) {
  const toggle = (key: Metric) => {
    const next = new Set(activeMetrics);
    if (next.has(key)) {
      if (next.size > 1) next.delete(key); // keep at least one
    } else {
      next.add(key);
    }
    onChange(next);
  };

  return (
    <div className="flex gap-1">
      {METRICS.map((m) => {
        const active = activeMetrics.has(m.key);
        return (
          <button
            key={m.key}
            onClick={(e) => { e.stopPropagation(); toggle(m.key); }}
            className="px-2.5 py-1 text-xs font-semibold rounded-full border cursor-pointer transition-colors duration-150"
            style={{
              backgroundColor: active ? m.color : 'transparent',
              borderColor: active ? m.color : '#ccc',
              color: active ? '#fff' : '#666',
            }}
          >
            {m.label}
          </button>
        );
      })}
    </div>
  );
}
