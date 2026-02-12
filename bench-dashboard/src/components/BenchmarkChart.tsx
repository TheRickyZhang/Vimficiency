import { Line } from 'react-chartjs-2';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';

const COLORS = ['#4285f4', '#ea4335', '#34a853', '#fbbc04', '#ff6d01',
  '#46bdc6', '#9c27b0', '#795548', '#607d8b', '#e91e63'];

interface Props {
  series: TimePoint[];
  unit: Unit;
  large?: boolean;
  onPointClick?: (sha: string) => void;
}

export function BenchmarkChart({ series, unit, large, onPointClick }: Props) {
  return (
    <Line
      data={{
        labels: series.map((s) => s.sha),
        datasets: [{
          data: series.map((s) => s.val / unit.d),
          borderColor: COLORS[0],
          backgroundColor: COLORS[0] + '18',
          fill: true,
          tension: 0.3,
          pointRadius: large ? 4 : 3,
          pointHoverRadius: large ? 7 : 5,
          borderWidth: 2,
        }],
      }}
      options={{
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 250 },
        onClick: onPointClick ? (_event, elements) => {
          if (elements.length > 0) {
            const idx = elements[0]!.index;
            const sha = series[idx]?.sha;
            if (sha) onPointClick(sha);
          }
        } : undefined,
        plugins: {
          legend: { display: false },
          tooltip: {
            titleFont: { size: 14, weight: 'bold' },
            bodyFont: { size: 13 },
            callbacks: {
              title: (ctx) => series[ctx[0]!.dataIndex]?.msg ?? '',
              label: (ctx) => (ctx.parsed.y ?? 0).toFixed(2) + ' ' + unit.l,
            },
          },
        },
        scales: {
          x: { ticks: { font: { size: large ? 12 : 10 }, maxTicksLimit: large ? 15 : 8 } },
          y: {
            title: { display: true, text: unit.l, font: { size: 13, weight: 'bold' } },
            ticks: { font: { size: large ? 12 : 11 } },
            beginAtZero: true,
          },
        },
      }}
    />
  );
}
