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
import { App } from '../components/App';

Chart.register(CategoryScale, LinearScale, PointElement, LineElement, Filler, Tooltip, zoomPlugin);

(async () => {
  try {
    const res = await fetch(`data.js?_=${Date.now()}`);
    const text = await res.text();
    new Function(text)();
  } catch {
    // App handles missing data gracefully
  }

  createRoot(document.getElementById('content')!).render(
    <StrictMode>
      <App />
    </StrictMode>,
  );
})();
