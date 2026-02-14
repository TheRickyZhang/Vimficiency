import { useState, useEffect, useCallback } from 'react';
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
  const storageKey = `vimficiency-hidden-${optimizerName}`;
  const [hiddenShas, setHiddenShas] = useState<Set<string>>(() => {
    try {
      const stored = localStorage.getItem(storageKey);
      return stored ? new Set(JSON.parse(stored)) : new Set();
    } catch {
      return new Set();
    }
  });

  useEffect(() => {
    if (hiddenShas.size > 0) {
      localStorage.setItem(storageKey, JSON.stringify([...hiddenShas]));
    } else {
      localStorage.removeItem(storageKey);
    }
  }, [hiddenShas, storageKey]);

  const onHide = useCallback((sha: string) => {
    setHiddenShas((prev) => new Set([...prev, sha]));
  }, []);

  const onResetHidden = useCallback(() => {
    setHiddenShas(new Set());
  }, []);

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
          hiddenShas={hiddenShas}
          onHide={onHide}
          onResetHidden={onResetHidden}
          onBack={() => onNavigate(null)}
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
