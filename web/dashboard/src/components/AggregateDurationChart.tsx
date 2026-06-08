import { useMemo, useState, type ReactNode } from 'react';
import { BenchmarkChart, type Metric } from './BenchmarkChart';
import { medianRelativeSeries, totalSeries, type TimePoint } from '../utils/data';
import { bestUnit } from '../utils/format';
import type { BenchmarkRun } from '../types/benchmark';

const RELATIVE_METRIC = new Set<Metric>(['relative']);
const RATIO_UNIT = { d: 1, l: '×' };

interface Props {
  data: BenchmarkRun[];
  repoUrl: string;
  label: string;
  // Tests have a recorded binary wall-time; optimizers fall back to summing benches.
  totalOverride?: TimePoint[];
}

export function AggregateDurationChart({ data, repoUrl, label, totalOverride }: Props) {
  const [mode, setMode] = useState<'median' | 'total'>('median');
  const medianSeries = useMemo(() => medianRelativeSeries(data, repoUrl), [data, repoUrl]);
  const total = useMemo(() => totalOverride ?? totalSeries(data, repoUrl), [totalOverride, data, repoUrl]);

  if ((mode === 'median' ? medianSeries : total).length === 0) return null;

  return (
    <section className="card p-4 mb-8 max-w-[60%] mx-auto max-md:max-w-full">
      <div className="flex items-center justify-between mb-2 gap-3">
        <h4 className="m-0 text-base font-bold text-[#333]">{label} timing</h4>
        <div className="flex gap-1">
          <ToggleButton active={mode === 'median'} onClick={() => setMode('median')}>Median</ToggleButton>
          <ToggleButton active={mode === 'total'} onClick={() => setMode('total')}>Total</ToggleButton>
        </div>
      </div>
      <div style={{ height: 220 }}>
        {mode === 'median'
          ? <BenchmarkChart series={medianSeries} unit={RATIO_UNIT} activeMetrics={RELATIVE_METRIC} />
          : <BenchmarkChart series={total} unit={bestUnit(total.map((p) => p.val))} />}
      </div>
      <p className="m-0 mt-2 text-xs text-muted">
        {mode === 'median'
          ? 'Median across tests of each test’s time relative to its first commit (=1.00). Each test weighted equally.'
          : 'Sum of every test’s time — dominated by the longest-running ones.'}
      </p>
    </section>
  );
}

function ToggleButton({ active, onClick, children }: { active: boolean; onClick: () => void; children: ReactNode }) {
  return (
    <button
      type="button"
      onClick={onClick}
      className="px-2.5 py-1 text-xs font-semibold rounded-full border cursor-pointer transition-colors duration-150"
      style={{
        backgroundColor: active ? '#34a853' : 'transparent',
        borderColor: active ? '#34a853' : '#ccc',
        color: active ? '#fff' : '#666',
      }}
    >
      {children}
    </button>
  );
}
