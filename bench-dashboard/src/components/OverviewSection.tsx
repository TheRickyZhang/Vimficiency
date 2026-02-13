import type { BenchmarkRun } from '../types/benchmark';
import { SummaryCard } from './SummaryCard';
import { ChangesSection } from './ChangesSection';
import styles from './OverviewSection.module.css';

interface Props {
  categories: Record<string, string[]>;
  data: BenchmarkRun[];
  onSelect: (category: string) => void;
  onSelectBench: (category: string, benchName: string) => void;
}

export function OverviewSection({ categories, data, onSelect, onSelectBench }: Props) {
  const entries = Object.entries(categories);
  return (
    <div>
      <ChangesSection categories={categories} data={data} onSelect={onSelectBench} />
      <h2>Categories</h2>
      <div className={styles.summaryGrid}>
        {entries.map(([cat, names]) => (
          <SummaryCard
            key={cat}
            category={cat}
            benchNames={names}
            data={data}
            onClick={() => onSelect(cat)}
          />
        ))}
      </div>
    </div>
  );
}
