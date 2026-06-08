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
import zoomPlugin from 'chartjs-plugin-zoom';
import { RouterProvider } from '@tanstack/react-router';
import { router } from './router';
import './index.css';

Chart.register(CategoryScale, LinearScale, PointElement, LineElement, BarElement, Filler, Tooltip, zoomPlugin);

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
);
