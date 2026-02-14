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
import { ExploreApp } from '../components/ExploreApp';

Chart.register(CategoryScale, LinearScale, PointElement, LineElement, BarElement, Filler, Tooltip);

async function loadExploreData() {
  // In production: explore page is at bench/{optimizer}/explore/index.html
  //   → ../explore.js resolves to bench/{optimizer}/explore.js
  // In dev: explore.html is at the root
  //   → explore.js resolves to public/explore.js via Vite
  for (const url of ['../explore.js', 'explore.js']) {
    try {
      const res = await fetch(`${url}?_=${Date.now()}`);
      if (!res.ok) continue;
      const text = await res.text();
      new Function(text)();
      if (window.EXPLORATION_DATA) return;
    } catch {
      continue;
    }
  }
}

(async () => {
  await loadExploreData();

  createRoot(document.getElementById('content')!).render(
    <StrictMode>
      <ExploreApp />
    </StrictMode>,
  );
})();
