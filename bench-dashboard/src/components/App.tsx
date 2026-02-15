import { useEffect } from 'react';
import type { BenchmarkRun } from '../types/benchmark';
import { OverviewSection } from './OverviewSection';
import { CategorySection } from './CategorySection';

interface Props {
  data: BenchmarkRun[];
  categories: Record<string, string[]>;
  optimizerName: string;
  optimizer: string;
  repoUrl: string;
  activeCategory: string | null;
  openBench: string | null;
  onNavigate: (cat: string | null, benchName?: string) => void;
  onBenchConsumed: () => void;
}

export function App({ data, categories, optimizerName, optimizer, repoUrl, activeCategory, openBench, onNavigate, onBenchConsumed }: Props) {
  useEffect(() => {
    document.title = `${optimizerName} — Vimficiency Benchmarks`;
  }, [optimizerName]);

  const validCategory = categories[activeCategory ?? ''] ? activeCategory : null;

  if (validCategory && categories[validCategory]) {
    return (
      <>
        <h1>{optimizerName}</h1>
        <p className="subtitle">Performance tracking across commits</p>
        <CategorySection
          category={validCategory}
          benchNames={categories[validCategory]}
          data={data}
          optimizer={optimizer}
          repoUrl={repoUrl}
          initialBench={openBench}
          onBenchOpened={onBenchConsumed}
        />
      </>
    );
  }

  return (
    <>
      <h1>{optimizerName}</h1>
      <p className="subtitle">Performance tracking across commits</p>
      <OverviewSection
        categories={categories}
        data={data}
        onSelect={(cat) => onNavigate(cat)}
        onSelectBench={(cat, bench) => onNavigate(cat, bench)}
      />
    </>
  );
}
