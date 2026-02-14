import { useState, useEffect, useCallback } from 'react';
import type { BenchmarkRun } from '../types/benchmark';
import { OverviewSection } from './OverviewSection';
import { CategorySection } from './CategorySection';

interface Props {
  data: BenchmarkRun[];
  categories: Record<string, string[]>;
  optimizerName: string;
  repoUrl: string;
}

export function App({ data, categories, optimizerName, repoUrl }: Props) {
  const [activeCategory, setActiveCategory] = useState<string | null>(
    () => decodeURIComponent(location.hash.substring(1)) || null,
  );
  const [openBench, setOpenBench] = useState<string | null>(
    () => new URLSearchParams(location.search).get('bench'),
  );

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

  const navigate = useCallback((cat: string | null, benchName?: string) => {
    location.hash = cat ?? '';
    setActiveCategory(cat);
    setOpenBench(benchName ?? null);
    window.scrollTo(0, 0);
  }, []);

  useEffect(() => {
    const handler = () => {
      setActiveCategory(decodeURIComponent(location.hash.substring(1)) || null);
      setOpenBench(null);
    };
    window.addEventListener('hashchange', handler);
    return () => window.removeEventListener('hashchange', handler);
  }, []);

  useEffect(() => {
    const title = document.getElementById('optimizer-title');
    const page = document.getElementById('page-title');
    if (title) title.textContent = optimizerName;
    if (page) page.textContent = optimizerName;
    document.title = `${optimizerName} — Vimficiency Benchmarks`;
  }, [optimizerName]);

  const catNames = categories[activeCategory ?? ''] ? activeCategory : null;

  if (catNames && categories[catNames]) {
    return (
      <CategorySection
        category={catNames}
        benchNames={categories[catNames]}
        data={data}
        repoUrl={repoUrl}
        hiddenShas={hiddenShas}
        onHide={onHide}
        onResetHidden={onResetHidden}
        onBack={() => navigate(null)}
        initialBench={openBench}
        onBenchOpened={() => setOpenBench(null)}
      />
    );
  }

  return (
    <OverviewSection
      categories={categories}
      data={data}
      onSelect={(cat) => navigate(cat)}
      onSelectBench={(cat, bench) => navigate(cat, bench)}
    />
  );
}
