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

(async () => {
  // Load explore.js from parent directory (bench/{optimizer}/explore.js)
  try {
    const res = await fetch(`../explore.js?_=${Date.now()}`);
    const text = await res.text();
    new Function(text)();
  } catch {
    // ExploreApp handles missing data gracefully
  }

  createRoot(document.getElementById('content')!).render(
    <StrictMode>
      <ExploreApp />
    </StrictMode>,
  );
})();
