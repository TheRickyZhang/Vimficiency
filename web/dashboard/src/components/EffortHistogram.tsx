import { useMemo } from 'react';
import { Bar } from 'react-chartjs-2';
import type { ExploredStateEntry, FoundResultEntry } from '../types/exploration';

interface Props {
  states: ExploredStateEntry[];
  results?: FoundResultEntry[];
  selectedSeq?: string | null;
}

export function EffortHistogram({ states, results, selectedSeq }: Props) {
  if (!states.length) return null;

  // Skip trivial starting state (effort 0 / depth 0)
  const efforts = states.map((s) => s.effort).filter((e) => e > 0);
  if (!efforts.length) return null;

  const minE = Math.min(...efforts);
  const maxE = Math.max(...efforts);

  // Integer-unit bins: each bin spans 1 unit of effort
  const binStart = Math.floor(minE);
  const binEnd = Math.ceil(maxE);
  const numBins = Math.max(1, binEnd - binStart);

  const counts = new Array<number>(numBins).fill(0);
  for (const e of efforts) {
    const idx = Math.min(Math.floor(e - binStart), numBins - 1);
    counts[idx]!++;
  }

  const labels = counts.map((_, i) => String(binStart + i));

  const barColors = useMemo(() => {
    const resultEfforts = (results ?? []).map((r) => r.effort);
    const selectedEffort = selectedSeq
      ? results?.find((r) => r.tokens.join('') === selectedSeq)?.effort ?? null
      : null;

    return counts.map((_, i) => {
      const binLo = binStart + i;
      const binHi = binStart + i + 1;
      const inBin = (e: number) => (i === numBins - 1) ? (e >= binLo && e <= binHi) : (e >= binLo && e < binHi);

      if (selectedEffort !== null && inBin(selectedEffort)) return '#1565c0'; // selected: dark blue
      if (resultEfforts.some(inBin)) return '#43a047'; // found result: vivid green
      return '#b0c4de'; // default: muted blue-gray
    });
  }, [counts, results, selectedSeq, binStart, numBins]);

  return (
    <div style={{ height: 260 }}>
      <Bar
        data={{
          labels,
          datasets: [{
            data: counts,
            backgroundColor: barColors,
            borderRadius: 2,
          }],
        }}
        options={{
          responsive: true,
          maintainAspectRatio: false,
          animation: { duration: 200 },
          layout: { padding: { top: 16 } },
          plugins: {
            legend: { display: false },
            tooltip: {
              callbacks: {
                title: (ctx) => {
                  const i = ctx[0]!.dataIndex;
                  return `Effort ${binStart + i} – ${binStart + i + 1}`;
                },
                label: (ctx) => `${ctx.parsed.y} states`,
              },
            },
          },
          scales: {
            x: {
              title: { display: true, text: 'Effort', font: { weight: 'bold' as const } },
              ticks: { maxTicksLimit: 20, font: { size: 10 } },
            },
            y: {
              title: { display: true, text: 'Count', font: { weight: 'bold' as const } },
              beginAtZero: true,
            },
          },
        }}
      />
    </div>
  );
}
