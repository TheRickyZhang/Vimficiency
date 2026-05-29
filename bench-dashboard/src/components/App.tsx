import { useEffect } from 'react';
import type { ReactNode } from 'react';
import { Link } from '@tanstack/react-router';
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
  exploreCategories?: string[] | null;
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
  exploreCategories,
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

  // Optimizer-level Search Space entry. Per-chart buttons stay gated per category;
  // this keeps explore reachable on any optimizer that has data, since the chart
  // buttons vanish for categories absent from the (curated, smaller) explore set.
  // null = explore.json unavailable (unknown) → show, matching the inline buttons'
  // don't-gate-when-unknown rule; [] → hide.
  const hasExplore = showExplore && (exploreCategories == null || exploreCategories.length > 0);
  const pageHeader = (
    <>
      <div className="flex items-baseline justify-between gap-4">
        <h1>{optimizerName}</h1>
        {hasExplore && (
          <Link to="/$optimizer/explore" params={{ optimizer }} search={{ case: undefined }} className="explore-btn">
            Search Space
          </Link>
        )}
      </div>
      <p className="subtitle">{pageSubtitle}</p>
    </>
  );

  if (validCategory && categories[validCategory]) {
    return (
      <>
        {pageHeader}
        {inlinePanel}
        <CategorySection
          category={validCategory}
          benchNames={categories[validCategory]}
          data={data}
          optimizer={optimizer}
          repoUrl={repoUrl}
          initialBench={openBench}
          showExplore={showExplore}
          exploreCategories={exploreCategories}
          categoryActionLabel={categoryActionLabel}
          onCategoryAction={onCategoryAction}
          onBenchOpened={onBenchConsumed}
        />
      </>
    );
  }

  return (
    <>
      {pageHeader}
      {headerChart}
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
