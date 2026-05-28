import { Line } from 'react-chartjs-2';
import type { BenchmarkRun } from '../types/benchmark';
import { toNanoseconds } from '../utils/data';
import { bestUnit, type Nanoseconds } from '../utils/format';

export interface ChartLine {
  label: string;
  benchName: string;
  color: string;
}

// Plots one wallclock line per ChartLine over a shared commit x-axis, leaving
// gaps where a commit lacks that bench (e.g. legacy unit-only entries with no
// Approval total).
export function MultiLineChart({ runs, lines }: { runs: BenchmarkRun[]; lines: ChartLine[] }) {
  const labels = runs.map((r) => r.commit.id.substring(0, 7));
  const msgs = runs.map((r) => r.commit.message.split('\n')[0] ?? '');

  const valNs = (run: BenchmarkRun, name: string): Nanoseconds | null => {
    const b = run.benches.find((x) => x.name === name);
    return b ? toNanoseconds(b.value, b.unit) : null;
  };

  const allVals = lines.flatMap((l) =>
    runs.map((r) => valNs(r, l.benchName)).filter((v): v is Nanoseconds => v != null),
  );
  const unit = bestUnit(allVals);

  const datasets = lines.map((l) => ({
    label: l.label,
    data: runs.map((r) => {
      const v = valNs(r, l.benchName);
      return v == null ? null : v / unit.d;
    }),
    borderColor: l.color,
    backgroundColor: l.color + '18',
    tension: 0.3,
    pointRadius: 3,
    pointHoverRadius: 5,
    borderWidth: 2,
    spanGaps: false,
  }));

  return (
    <Line
      data={{ labels, datasets }}
      options={{
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 250 },
        interaction: { mode: 'index', intersect: false },
        plugins: {
          legend: { display: true },
          tooltip: {
            mode: 'index',
            intersect: false,
            titleFont: { size: 14, weight: 'bold' as const },
            bodyFont: { size: 13 },
            callbacks: {
              title: (ctx) => msgs[ctx[0]!.dataIndex] ?? '',
              label: (ctx) =>
                ctx.parsed.y == null ? '' : `${ctx.dataset.label}: ${ctx.parsed.y.toFixed(2)} ${unit.l}`,
            },
          },
        },
        scales: {
          x: { ticks: { color: '#4285f4', font: { size: 10 }, maxTicksLimit: 8 } },
          y: {
            title: { display: true, text: unit.l, font: { size: 13, weight: 'bold' as const } },
            ticks: { font: { size: 11 } },
            beginAtZero: true,
          },
        },
      }}
    />
  );
}
