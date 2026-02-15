import type { BenchmarkRun } from '../types/benchmark';
import { SummaryCard } from './SummaryCard';
import { ChangesSection } from './ChangesSection';

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
      <h2>Categories</h2>
      <div className="grid grid-cols-[repeat(auto-fit,minmax(200px,1fr))] gap-3 mb-8">
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
      <ChangesSection categories={categories} data={data} onSelect={onSelectBench} />
    </div>
  );
}
