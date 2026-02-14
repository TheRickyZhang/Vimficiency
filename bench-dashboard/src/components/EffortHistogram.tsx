import { Bar } from 'react-chartjs-2';
import type { ExploredStateEntry } from '../types/exploration';

interface Props {
  states: ExploredStateEntry[];
}

const BINS = 40;

export function EffortHistogram({ states }: Props) {
  if (!states.length) return null;

  const efforts = states.map((s) => s.effort);
  const min = Math.min(...efforts);
  const max = Math.max(...efforts);
  const range = max - min || 1;
  const binSize = range / BINS;

  const counts = new Array<number>(BINS).fill(0);
  for (const e of efforts) {
    const idx = Math.min(Math.floor((e - min) / binSize), BINS - 1);
    counts[idx]!++;
  }

  const labels = counts.map((_, i) => (min + (i + 0.5) * binSize).toFixed(1));

  return (
    <div style={{ height: 260 }}>
      <Bar
        data={{
          labels,
          datasets: [{
            data: counts,
            backgroundColor: '#4285f4',
            borderRadius: 2,
          }],
        }}
        options={{
          responsive: true,
          maintainAspectRatio: false,
          animation: { duration: 200 },
          plugins: {
            legend: { display: false },
            tooltip: {
              callbacks: {
                title: (ctx) => {
                  const i = ctx[0]!.dataIndex;
                  const lo = (min + i * binSize).toFixed(2);
                  const hi = (min + (i + 1) * binSize).toFixed(2);
                  return `Effort ${lo} - ${hi}`;
                },
                label: (ctx) => `${ctx.parsed.y} states`,
              },
            },
          },
          scales: {
            x: {
              title: { display: true, text: 'Effort', font: { weight: 'bold' as const } },
              ticks: { maxTicksLimit: 10, font: { size: 10 } },
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
