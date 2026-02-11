import { useMemo } from 'react';
import type { BenchmarkRun } from '../types/benchmark';
import { getData, discoverCategories } from '../utils/data';

interface BenchmarkDataResult {
  data: BenchmarkRun[];
  categories: Record<string, string[]>;
  optimizerName: string;
}

export function useBenchmarkData(): BenchmarkDataResult {
  return useMemo(() => {
    const data = getData();
    const categories = discoverCategories(data);

    let optimizerName = 'Benchmarks';
    if (data.length) {
      const firstName = data[data.length - 1]!.benches[0]?.name;
      if (firstName) {
        optimizerName = firstName.split('/')[0] ?? 'Benchmarks';
      }
    }

    return { data, categories, optimizerName };
  }, []);
}
