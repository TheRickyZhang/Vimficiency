import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import {
  Chart,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  BarElement,
  Filler,
  Tooltip,
} from 'chart.js';
import type { ExplorationData } from '../types/exploration';
import { ExploreApp } from '../components/ExploreApp';

Chart.register(CategoryScale, LinearScale, PointElement, LineElement, BarElement, Filler, Tooltip);

async function loadExploreData(): Promise<ExplorationData> {
  // In production: explore page is at bench/{optimizer}/explore/index.html
  //   -> ../explore.json resolves to bench/{optimizer}/explore.json
  // In dev: explore.html is at the root
  //   -> explore.json resolves to public/explore.json via Vite
  for (const url of ['../explore.json', 'explore.json']) {
    try {
      const res = await fetch(`${url}?_=${Date.now()}`);
      if (!res.ok) continue;
      return await res.json();
    } catch {
      continue;
    }
  }
  throw new Error('Failed to load exploration data');
}

(async () => {
  const data = await loadExploreData();

  createRoot(document.getElementById('content')!).render(
    <StrictMode>
      <ExploreApp data={data} />
    </StrictMode>,
  );
})();
