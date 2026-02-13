import { useRef, useCallback, useEffect } from 'react';
import { Line } from 'react-chartjs-2';
import type { TooltipModel, Chart, ChartEvent } from 'chart.js';
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

function isXAxisClick(chart: Chart, event: ChartEvent): number | null {
  const { x, y } = event;
  if (x == null || y == null) return null;
  if (y <= chart.chartArea.bottom) return null;
  const idx = Math.round(chart.scales.x!.getValueForPixel(x)!);
  if (idx < 0 || idx >= chart.data.labels!.length) return null;
  return idx;
}

export function BenchmarkChart({ series, unit, large, onPointClick }: Props) {
  const tooltipRef = useRef<HTMLDivElement | null>(null);
  const hideTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const mouseInTooltipRef = useRef(false);

  // Cleanup tooltip on unmount
  useEffect(() => {
    return () => {
      if (hideTimeoutRef.current) clearTimeout(hideTimeoutRef.current);
      if (tooltipRef.current) {
        tooltipRef.current.remove();
        tooltipRef.current = null;
      }
    };
  }, []);

  const externalTooltip = useCallback((context: { chart: Chart; tooltip: TooltipModel<'line'> }) => {
    const { chart, tooltip } = context;
    let el = tooltipRef.current;
    if (!el) {
      el = document.createElement('div');
      el.style.cssText = 'position:absolute;pointer-events:auto;background:#1a1a2e;color:#fff;border-radius:8px;font-size:13px;box-shadow:0 4px 12px rgba(0,0,0,0.3);z-index:9999;transition:opacity 0.15s;max-width:350px';
      // Padding includes extra bottom padding to bridge gap to data point
      el.style.padding = '10px 14px 18px 14px';
      el.addEventListener('mouseenter', () => {
        mouseInTooltipRef.current = true;
        if (hideTimeoutRef.current) {
          clearTimeout(hideTimeoutRef.current);
          hideTimeoutRef.current = null;
        }
      });
      el.addEventListener('mouseleave', () => {
        mouseInTooltipRef.current = false;
        el!.style.opacity = '0';
        el!.style.pointerEvents = 'none';
      });
      chart.canvas.parentNode!.appendChild(el);
      tooltipRef.current = el;
    }

    if (tooltip.opacity === 0) {
      // Delay hiding so user can move mouse into tooltip
      if (!mouseInTooltipRef.current && !hideTimeoutRef.current) {
        hideTimeoutRef.current = setTimeout(() => {
          hideTimeoutRef.current = null;
          if (!mouseInTooltipRef.current && el) {
            el.style.opacity = '0';
            el.style.pointerEvents = 'none';
          }
        }, 200);
      }
      return;
    }

    // Cancel any pending hide since tooltip is being shown for a (new) point
    if (hideTimeoutRef.current) {
      clearTimeout(hideTimeoutRef.current);
      hideTimeoutRef.current = null;
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
    // Position so bottom of tooltip (including bridge padding) reaches the point
    el.style.left = offsetLeft + tooltip.caretX + 'px';
    el.style.top = offsetTop + tooltip.caretY - el.offsetHeight + 'px';
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
          pointHoverRadius: large ? 8 : 5,
          pointHitRadius: large ? 20 : 10,
          borderWidth: 2,
        }],
      }}
      options={{
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 250 },
        onClick: (event, elements, chart) => {
          const axisIdx = isXAxisClick(chart, event);
          if (axisIdx != null) {
            const url = series[axisIdx]?.commitUrl;
            if (url) {
              const native = event.native as any;
              if (native) native.__xAxisHandled = true;
              window.open(url, '_blank');
            }
            return;
          }
          if (onPointClick && elements.length > 0) {
            const idx = elements[0]!.index;
            const sha = series[idx]?.sha;
            if (sha) onPointClick(sha);
          }
        },
        onHover: (event, _elements, chart) => {
          const axisIdx = isXAxisClick(chart, event);
          chart.canvas.style.cursor = axisIdx != null ? 'pointer' : '';
        },
        plugins: {
          legend: { display: false },
          tooltip: large ? {
            enabled: false,
            external: externalTooltip,
          } : {
            titleFont: { size: 14, weight: 'bold' as const },
            bodyFont: { size: 13 },
            callbacks: {
              title: (ctx) => series[ctx[0]!.dataIndex]?.msg ?? '',
              label: (ctx) => (ctx.parsed.y ?? 0).toFixed(2) + ' ' + unit.l,
            },
          },
          zoom: large ? {
            pan: {
              enabled: true,
              mode: 'x' as const,
              modifierKey: undefined,
            },
            zoom: {
              wheel: { enabled: true },
              pinch: { enabled: true },
              mode: 'x' as const,
            },
          } : undefined,
        },
        scales: {
          x: { ticks: { color: '#4285f4', font: { size: large ? 12 : 10 }, maxTicksLimit: large ? 15 : 8 } },
          y: {
            title: { display: true, text: unit.l, font: { size: 13, weight: 'bold' as const } },
            ticks: { font: { size: large ? 12 : 11 } },
            beginAtZero: true,
          },
        },
      }}
    />
  );
}
