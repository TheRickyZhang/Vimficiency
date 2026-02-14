import { useState, useEffect } from 'react';
import { Link } from '@tanstack/react-router';
import type { BenchmarkRun } from '../types/benchmark';
import { parseName, timeSeries } from '../utils/data';
import { bestUnit } from '../utils/format';
import { BenchmarkChart } from './BenchmarkChart';
import { ChartModal } from './ChartModal';

interface Props {
  category: string;
  benchNames: string[];
  data: BenchmarkRun[];
  optimizer: string;
  repoUrl: string;
  hiddenShas: Set<string>;
  onHide: (sha: string) => void;
  onResetHidden: () => void;
  onBack: () => void;
  initialBench?: string | null;
  onBenchOpened?: () => void;
}

export function CategorySection({ category, benchNames, data, optimizer, repoUrl, hiddenShas, onHide, onResetHidden, onBack, initialBench, onBenchOpened }: Props) {
  const [modal, setModal] = useState<{ title: string; idx: number } | null>(null);

  const charts = benchNames.map((benchName) => {
    const detail = parseName(benchName).detail;
    const series = timeSeries(data, benchName, repoUrl, hiddenShas);
    const unit = bestUnit(series.map((s) => s.val));
    return { benchName, detail, series, unit };
  }).filter((c) => c.series.length > 0);

  // Auto-open modal for a specific benchmark when navigating from changes section
  useEffect(() => {
    if (initialBench) {
      const idx = charts.findIndex((c) => c.benchName === initialBench);
      if (idx >= 0) {
        setModal({ title: charts[idx]!.detail, idx });
      }
      onBenchOpened?.();
    }
  }, [initialBench]); // eslint-disable-line react-hooks/exhaustive-deps

  const modalData = modal !== null ? charts[modal.idx] : undefined;

  return (
    <div>
      <div className="flex items-center gap-4 mb-5">
        <a href="#" className="text-[0.95rem] text-brand no-underline font-semibold hover:underline" onClick={(e) => { e.preventDefault(); onBack(); }}>
          &larr; All categories
        </a>
        <h2>{category}</h2>
      </div>
      <div className="grid grid-cols-2 gap-4 mb-10 max-md:grid-cols-1">
        {charts.map((c, i) => (
          <div key={c.benchName} className="bg-surface border border-border rounded-lg p-4 cursor-default transition-[box-shadow,border-color] duration-150 hover:border-brand hover:shadow-[0_2px_8px_rgba(0,0,0,0.1)]" onClick={(e) => {
            if (!(e.nativeEvent as any).__xAxisHandled) setModal({ title: c.detail, idx: i }); // eslint-disable-line @typescript-eslint/no-explicit-any
          }}>
            <div className="flex justify-between items-center mb-2">
              <h4 className="m-0 text-base font-bold text-[#333]">{c.detail}</h4>
              <Link
                to="/$optimizer/explore"
                params={{ optimizer }}
                search={{ case: category + '/' + c.detail }}
                onClick={(e) => e.stopPropagation()}
                title="Explore search space"
                className="text-xs text-brand no-underline font-semibold px-2 py-0.5 rounded border border-[#d0d0d0] bg-[#f8f9fa] whitespace-nowrap cursor-pointer"
              >
                Explore
              </Link>
            </div>
            <div className="relative h-[200px]">
              <BenchmarkChart series={c.series} unit={c.unit} />
            </div>
          </div>
        ))}
      </div>
      {modalData && (
        <ChartModal
          title={modalData.detail}
          series={modalData.series}
          unit={modalData.unit}
          hiddenShas={hiddenShas}
          repoUrl={repoUrl}
          onPointClick={onHide}
          onResetHidden={onResetHidden}
          onClose={() => setModal(null)}
        />
      )}
    </div>
  );
}
