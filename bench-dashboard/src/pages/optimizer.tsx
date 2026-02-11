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
import { App } from '../components/App';

Chart.register(CategoryScale, LinearScale, PointElement, LineElement, Filler, Tooltip);

createRoot(document.getElementById('content')!).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
