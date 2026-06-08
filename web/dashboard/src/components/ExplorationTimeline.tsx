import { Line } from 'react-chartjs-2';
import type { ExploredStateEntry } from '../types/exploration';

interface Props {
  states: ExploredStateEntry[];
}

export function ExplorationTimeline({ states }: Props) {
  if (!states.length) return null;

  const data = states.map((s, i) => ({ x: i, y: s.effort }));
  const large = states.length > 200;

  return (
    <div style={{ height: 260 }}>
      <Line
        data={{
          datasets: [{
            data,
            borderColor: '#34a853',
            backgroundColor: '#34a85318',
            fill: true,
            tension: 0,
            pointRadius: large ? 0 : 2,
            pointHoverRadius: large ? 3 : 5,
            borderWidth: large ? 1 : 2,
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
                  const idx = ctx[0]!.dataIndex;
                  return `State #${idx}`;
                },
                label: (ctx) => {
                  const idx = ctx.dataIndex;
                  const s = states[idx];
                  if (!s) return '';
                  return [`Effort: ${s.effort.toFixed(3)}`, `Seq: ${s.tokens.join('') || '(start)'}`];
                },
              },
            },
            decimation: large ? {
              enabled: true,
              algorithm: 'lttb' as const,
              samples: 500,
            } : undefined,
          },
          scales: {
            x: {
              type: 'linear' as const,
              title: { display: true, text: 'Exploration order', font: { weight: 'bold' as const } },
              ticks: { font: { size: 10 } },
            },
            y: {
              title: { display: true, text: 'Effort', font: { weight: 'bold' as const } },
              beginAtZero: true,
            },
          },
        }}
      />
    </div>
  );
}
