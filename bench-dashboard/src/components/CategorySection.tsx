import { useState } from 'react';
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
  hiddenShas: Set<string>;
  onHide: (sha: string) => void;
  onResetHidden: () => void;
  onBack: () => void;
}

export function CategorySection({ category, benchNames, data, hiddenShas, onHide, onResetHidden, onBack }: Props) {
  const [modal, setModal] = useState<{ title: string; idx: number } | null>(null);

  const charts = benchNames.map((benchName) => {
    const detail = parseName(benchName).detail;
    const series = timeSeries(data, benchName, hiddenShas);
    const unit = bestUnit(series.map((s) => s.val));
    return { benchName, detail, series, unit };
  }).filter((c) => c.series.length > 0);

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
          <div key={c.benchName} className={styles.chartCard} onClick={() => setModal({ title: c.detail, idx: i })}>
            <h4>{c.detail}</h4>
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
          onPointClick={onHide}
          onResetHidden={onResetHidden}
          onClose={() => setModal(null)}
        />
      )}
    </div>
  );
}
