import { useState, useEffect } from 'react';
import { Link } from '@tanstack/react-router';
import type { BenchmarkRun } from '../types/benchmark';
import { parseName, timeSeries } from '../utils/data';
import { bestUnit } from '../utils/format';
import { BenchmarkChart } from './BenchmarkChart';
import type { Metric } from './BenchmarkChart';
import { MetricToggles } from './MetricToggles';
import { ChartModal } from './ChartModal';

interface Props {
  category: string;
  benchNames: string[];
  data: BenchmarkRun[];
  optimizer: string;
  repoUrl: string;
  initialBench?: string | null;
  showExplore?: boolean;
  categoryActionLabel?: string;
  onCategoryAction?: (category: string) => void;
  onBenchOpened?: () => void;
}

function defaultMetrics(charts: { series: { val: number }[] }[]): Set<Metric> {
  // Default to relative, but fall back to wallclock if all series have <=1 point
  const hasMultiPoint = charts.some((c) => c.series.length > 1);
  return new Set<Metric>([hasMultiPoint ? 'relative' : 'wallclock']);
}

export function CategorySection({
  category,
  benchNames,
  data,
  optimizer,
  repoUrl,
  initialBench,
  showExplore = true,
  categoryActionLabel,
  onCategoryAction,
  onBenchOpened,
}: Props) {
  const [modal, setModal] = useState<{ title: string; idx: number } | null>(null);

  const charts = benchNames.map((benchName) => {
    const detail = parseName(benchName).detail;
    const series = timeSeries(data, benchName, repoUrl);
    const unit = bestUnit(series.map((s) => s.val));
    return { benchName, detail, series, unit };
  }).filter((c) => c.series.length > 0);

  const [activeMetrics, setActiveMetrics] = useState<Set<Metric>>(() => defaultMetrics(charts));

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
      <div className="flex justify-between items-center mb-3">
        <h2 className="mb-0">{category}</h2>
        <div className="flex items-center gap-3">
          {categoryActionLabel && onCategoryAction && (
            <button
              type="button"
              className="explore-btn"
              onClick={() => onCategoryAction(category)}
            >
              {categoryActionLabel}
            </button>
          )}
          <MetricToggles activeMetrics={activeMetrics} onChange={setActiveMetrics} />
        </div>
      </div>
      <div className="grid grid-cols-2 gap-4 mb-10 max-md:grid-cols-1">
        {charts.map((c, i) => (
          <div key={c.benchName} className="card card-hover p-4 cursor-default" onClick={(e) => {
            if (!(e.nativeEvent as any).__xAxisHandled) setModal({ title: c.detail, idx: i }); // eslint-disable-line @typescript-eslint/no-explicit-any
          }}>
            <div className="flex justify-between items-center mb-2">
              <h4 className="m-0 text-base font-bold text-[#333]">{c.detail}</h4>
              {showExplore && (
                <Link
                  to="/$optimizer/explore"
                  params={{ optimizer }}
                  search={{ case: category + '/' + c.detail }}
                  onClick={(e) => e.stopPropagation()}
                  title="Explore search space"
                  className="explore-btn"
                >
                  Explore
                </Link>
              )}
            </div>
            <div className="relative h-[200px]">
              <BenchmarkChart series={c.series} unit={c.unit} activeMetrics={activeMetrics} />
            </div>
          </div>
        ))}
      </div>
      {modalData && (
        <ChartModal
          title={modalData.detail}
          series={modalData.series}
          unit={modalData.unit}
          onClose={() => setModal(null)}
          exploreLink={showExplore ? { optimizer, case: category + '/' + modalData.detail } : undefined}
          activeMetrics={activeMetrics}
          onMetricsChange={setActiveMetrics}
        />
      )}
    </div>
  );
}
