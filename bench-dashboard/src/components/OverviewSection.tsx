import type { BenchmarkRun } from '../types/benchmark';
import { SummaryCard } from './SummaryCard';
import styles from './OverviewSection.module.css';

interface Props {
  categories: Record<string, string[]>;
  data: BenchmarkRun[];
  onSelect: (category: string) => void;
}

export function OverviewSection({ categories, data, onSelect }: Props) {
  const entries = Object.entries(categories);
  return (
    <div>
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
