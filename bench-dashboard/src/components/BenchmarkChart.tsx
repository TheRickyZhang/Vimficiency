import { useRef, useCallback } from 'react';
import { Line } from 'react-chartjs-2';
import type { TooltipModel, Chart } from 'chart.js';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';

const COLORS = ['#4285f4', '#ea4335', '#34a853', '#fbbc04', '#ff6d01',
  '#46bdc6', '#9c27b0', '#795548', '#607d8b', '#e91e63'];

interface Props {
  series: TimePoint[];
  unit: Unit;
  large?: boolean;
  onPointClick?: (sha: string) => void;
}

export function BenchmarkChart({ series, unit, large, onPointClick }: Props) {
  const tooltipRef = useRef<HTMLDivElement | null>(null);

  const externalTooltip = useCallback((context: { chart: Chart; tooltip: TooltipModel<'line'> }) => {
    const { chart, tooltip } = context;
    let el = tooltipRef.current;
    if (!el) {
      el = document.createElement('div');
      el.style.cssText = 'position:absolute;pointer-events:auto;background:#1a1a2e;color:#fff;border-radius:8px;padding:10px 14px;font-size:13px;box-shadow:0 4px 12px rgba(0,0,0,0.3);z-index:9999;transition:opacity 0.15s;max-width:350px';
      chart.canvas.parentNode!.appendChild(el);
      tooltipRef.current = el;
    }

    if (tooltip.opacity === 0) {
      el.style.opacity = '0';
      el.style.pointerEvents = 'none';
      return;
    }

    const idx = tooltip.dataPoints?.[0]?.dataIndex;
    if (idx == null) return;
    const point = series[idx];
    if (!point) return;

    const valText = (point.val / unit.d).toFixed(2) + ' ' + unit.l;
    el.innerHTML = `<div style="font-weight:600;margin-bottom:4px;line-height:1.3">${point.msg}</div>`
      + `<div style="margin-bottom:4px">${valText}</div>`
      + `<a href="${point.commitUrl}" target="_blank" rel="noopener" style="color:#7cb3ff;text-decoration:none;font-family:monospace;font-size:12px" onmouseenter="this.style.textDecoration='underline'" onmouseleave="this.style.textDecoration='none'">${point.sha}</a>`;

    el.style.opacity = '1';
    el.style.pointerEvents = 'auto';

    const { offsetLeft, offsetTop } = chart.canvas;
    el.style.left = offsetLeft + tooltip.caretX + 'px';
    el.style.top = offsetTop + tooltip.caretY - el.offsetHeight - 12 + 'px';
  }, [series, unit]);

  return (
    <Line
      data={{
        labels: series.map((s) => s.sha),
        datasets: [{
          data: series.map((s) => s.val / unit.d),
          borderColor: COLORS[0],
          backgroundColor: COLORS[0] + '18',
          fill: true,
          tension: 0.3,
          pointRadius: large ? 4 : 3,
          pointHoverRadius: large ? 7 : 5,
          borderWidth: 2,
        }],
      }}
      options={{
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 250 },
        onClick: onPointClick ? (_event, elements) => {
          if (elements.length > 0) {
            const idx = elements[0]!.index;
            const sha = series[idx]?.sha;
            if (sha) onPointClick(sha);
          }
        } : undefined,
        plugins: {
          legend: { display: false },
          tooltip: large ? {
            enabled: false,
            external: externalTooltip,
          } : {
            titleFont: { size: 14, weight: 'bold' },
            bodyFont: { size: 13 },
            callbacks: {
              title: (ctx) => series[ctx[0]!.dataIndex]?.msg ?? '',
              label: (ctx) => (ctx.parsed.y ?? 0).toFixed(2) + ' ' + unit.l,
            },
          },
        },
        scales: {
          x: { ticks: { font: { size: large ? 12 : 10 }, maxTicksLimit: large ? 15 : 8 } },
          y: {
            title: { display: true, text: unit.l, font: { size: 13, weight: 'bold' } },
            ticks: { font: { size: large ? 12 : 11 } },
            beginAtZero: true,
          },
        },
      }}
    />
  );
}
