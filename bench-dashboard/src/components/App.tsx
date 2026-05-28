import { useEffect } from 'react';
import type { ReactNode } from 'react';
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
  showExplore?: boolean;
  subtitle?: string;
  inlinePanel?: ReactNode;
  headerChart?: ReactNode;
  categoryActionLabel?: string;
  onCategoryAction?: (category: string) => void;
  onNavigate: (cat: string | null, benchName?: string) => void;
  onBenchConsumed: () => void;
}

export function App({
  data,
  categories,
  optimizerName,
  optimizer,
  repoUrl,
  activeCategory,
  openBench,
  showExplore = true,
  subtitle,
  inlinePanel,
  headerChart,
  categoryActionLabel,
  onCategoryAction,
  onNavigate,
  onBenchConsumed,
}: Props) {
  useEffect(() => {
    document.title = `${optimizerName} — Vimficiency Benchmarks`;
  }, [optimizerName]);

  const validCategory = categories[activeCategory ?? ''] ? activeCategory : null;
  const defaultSubtitle = optimizer === 'tests'
    ? 'Test duration tracking across commits'
    : 'Performance tracking across commits';
  const pageSubtitle = subtitle ?? defaultSubtitle;

  if (validCategory && categories[validCategory]) {
    return (
      <>
        <h1>{optimizerName}</h1>
        <p className="subtitle">{pageSubtitle}</p>
        {headerChart}
        {inlinePanel}
        <CategorySection
          category={validCategory}
          benchNames={categories[validCategory]}
          data={data}
          optimizer={optimizer}
          repoUrl={repoUrl}
          initialBench={openBench}
          showExplore={showExplore}
          categoryActionLabel={categoryActionLabel}
          onCategoryAction={onCategoryAction}
          onBenchOpened={onBenchConsumed}
        />
      </>
    );
  }

  return (
    <>
      <h1>{optimizerName}</h1>
      <p className="subtitle">{pageSubtitle}</p>
      {inlinePanel}
      <OverviewSection
        categories={categories}
        data={data}
        onSelect={(cat) => onNavigate(cat)}
        onSelectBench={(cat, bench) => onNavigate(cat, bench)}
        categoryActionLabel={categoryActionLabel}
        onCategoryAction={onCategoryAction}
      />
    </>
  );
}
