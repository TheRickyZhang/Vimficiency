import { useState, useEffect } from 'react';
import { Link } from '@tanstack/react-router';
import type { BenchmarkRun } from '../types/benchmark';
import { parseName, timeSeries } from '../utils/data';
import { bestUnit } from '../utils/format';
import { BenchmarkChart } from './BenchmarkChart';
import { ChartModal } from './ChartModal';
import styles from './CategorySection.module.css';

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
      <div className={styles.header}>
        <a href="#" className={styles.backLink} onClick={(e) => { e.preventDefault(); onBack(); }}>
          &larr; All categories
        </a>
        <h2>{category}</h2>
      </div>
      <div className={styles.chartGrid}>
        {charts.map((c, i) => (
          <div key={c.benchName} className={styles.chartCard} onClick={(e) => {
            if (!(e.nativeEvent as any).__xAxisHandled) setModal({ title: c.detail, idx: i }); // eslint-disable-line @typescript-eslint/no-explicit-any
          }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
              <h4 style={{ margin: 0 }}>{c.detail}</h4>
              <Link
                to="/$optimizer/explore"
                params={{ optimizer }}
                search={{ case: category + '/' + c.detail }}
                onClick={(e) => e.stopPropagation()}
                title="Explore search space"
                style={{
                  fontSize: '0.75rem', color: '#4285f4', textDecoration: 'none',
                  fontWeight: 600, padding: '2px 8px', borderRadius: 4,
                  border: '1px solid #d0d0d0', background: '#f8f9fa',
                  whiteSpace: 'nowrap',
                }}
              >
                Explore
              </Link>
            </div>
            <div className={styles.chartWrapper}>
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
