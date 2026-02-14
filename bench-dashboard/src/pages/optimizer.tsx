import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import {
  Chart,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Filler,
  Tooltip,
} from 'chart.js';
import zoomPlugin from 'chartjs-plugin-zoom';
import type { BenchmarkData } from '../types/benchmark';
import { App } from '../components/App';
import { loadBenchmarkData, discoverCategories } from '../utils/data';

Chart.register(CategoryScale, LinearScale, PointElement, LineElement, Filler, Tooltip, zoomPlugin);

(async () => {
  const res = await fetch(`data.json?_=${Date.now()}`);
  const raw: BenchmarkData = await res.json();

  const data = loadBenchmarkData(raw);
  const categories = discoverCategories(data);

  let optimizerName = 'Benchmarks';
  const firstName = data[data.length - 1]!.benches[0]?.name;
  if (firstName) {
    optimizerName = firstName.split('/')[0] ?? 'Benchmarks';
  }

  createRoot(document.getElementById('content')!).render(
    <StrictMode>
      <App data={data} categories={categories} optimizerName={optimizerName} repoUrl={raw.repoUrl} />
    </StrictMode>,
  );
})();
