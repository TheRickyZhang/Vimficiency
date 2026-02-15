import { useRef, useCallback, useEffect } from 'react';
import { Line } from 'react-chartjs-2';
import type { TooltipModel, Chart, ChartEvent, ChartDataset } from 'chart.js';
import type { Unit } from '../utils/format';
import type { TimePoint } from '../utils/data';
import { computeRelativeSeries } from '../utils/data';

export type Metric = 'wallclock' | 'instructions' | 'relative';

const METRIC_COLORS: Record<Metric, string> = {
  wallclock: '#4285f4',
  instructions: '#9c27b0',
  relative: '#34a853',
};

const METRIC_LABELS: Record<Metric, string> = {
  wallclock: 'Wall Clock',
  instructions: 'Instructions',
  relative: 'Relative',
};

interface Props {
  series: TimePoint[];
  unit: Unit;
  large?: boolean;
  activeMetrics?: Set<Metric>;
}

function isXAxisClick(chart: Chart, event: ChartEvent): number | null {
  const { x, y } = event;
  if (x == null || y == null) return null;
  if (y <= chart.chartArea.bottom) return null;
  const idx = Math.round(chart.scales.x!.getValueForPixel(x)!);
  if (idx < 0 || idx >= chart.data.labels!.length) return null;
  return idx;
}

function buildDatasets(series: TimePoint[], unit: Unit, activeMetrics: Set<Metric>, large?: boolean): ChartDataset<'line'>[] {
  const datasets: ChartDataset<'line'>[] = [];
  const pointRadius = large ? 4 : 3;
  const pointHoverRadius = large ? 8 : 5;
  const pointHitRadius = large ? 30 : 10;

  if (activeMetrics.has('wallclock')) {
    const color = METRIC_COLORS.wallclock;
    datasets.push({
      label: METRIC_LABELS.wallclock,
      data: series.map((s) => s.val / unit.d),
      borderColor: color,
      backgroundColor: color + '18',
      fill: activeMetrics.size === 1,
      tension: 0.3,
      pointRadius,
      pointHoverRadius,
      pointHitRadius,
      borderWidth: 2,
      yAxisID: 'y',
    });
  }

  if (activeMetrics.has('instructions')) {
    const color = METRIC_COLORS.instructions;
    const hasData = series.some((s) => s.instructions != null);
    if (hasData) {
      datasets.push({
        label: METRIC_LABELS.instructions,
        data: series.map((s) => s.instructions != null ? s.instructions / 1000 : null),
        borderColor: color,
        backgroundColor: color + '18',
        fill: false,
        tension: 0.3,
        pointRadius,
        pointHoverRadius,
        pointHitRadius,
        borderWidth: 2,
        yAxisID: activeMetrics.has('wallclock') ? 'y1' : 'y',
      });
    }
  }

  if (activeMetrics.has('relative')) {
    const color = METRIC_COLORS.relative;
    const relData = computeRelativeSeries(series);
    datasets.push({
      label: METRIC_LABELS.relative,
      data: relData,
      borderColor: color,
      backgroundColor: color + '18',
      fill: activeMetrics.size === 1,
      tension: 0.3,
      pointRadius,
      pointHoverRadius,
      pointHitRadius,
      borderWidth: 2,
      yAxisID: activeMetrics.has('wallclock') ? 'y1' : 'y',
    });
  }

  return datasets;
}

export function BenchmarkChart({ series, unit, large, activeMetrics: activeMetricsProp }: Props) {
  const activeMetrics = activeMetricsProp ?? new Set<Metric>(['wallclock']);
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

  const needsRightAxis = activeMetrics.has('wallclock') && (activeMetrics.has('instructions') || activeMetrics.has('relative'));

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

    const lines: string[] = [];
    if (activeMetrics.has('wallclock')) {
      lines.push(`<span style="color:${METRIC_COLORS.wallclock}">\u25CF</span> ${(point.val / unit.d).toFixed(2)} ${unit.l}`);
    }
    if (activeMetrics.has('instructions') && point.instructions != null) {
      lines.push(`<span style="color:${METRIC_COLORS.instructions}">\u25CF</span> ${(point.instructions / 1000).toFixed(1)}k instr`);
    }
    if (activeMetrics.has('relative')) {
      const relData = computeRelativeSeries(series);
      const relVal = relData[idx];
      if (relVal != null) {
        lines.push(`<span style="color:${METRIC_COLORS.relative}">\u25CF</span> ${relVal.toFixed(3)}\u00D7`);
      }
    }

    el.innerHTML = `<div style="font-weight:600;margin-bottom:4px;line-height:1.3">${point.msg}</div>`
      + `<div style="margin-bottom:4px">${lines.join('<br>')}</div>`
      + `<a href="${point.commitUrl}" target="_blank" rel="noopener" style="color:#7cb3ff;text-decoration:none;font-family:monospace;font-size:12px" onmouseenter="this.style.textDecoration='underline'" onmouseleave="this.style.textDecoration='none'">${point.sha}</a>`;

    el.style.opacity = '1';
    el.style.pointerEvents = 'auto';

    const { offsetLeft, offsetTop } = chart.canvas;
    // Position so bottom of tooltip (including bridge padding) reaches the point
    el.style.left = offsetLeft + tooltip.caretX + 'px';
    el.style.top = offsetTop + tooltip.caretY - el.offsetHeight + 'px';
  }, [series, unit, activeMetrics]);

  const datasets = buildDatasets(series, unit, activeMetrics, large);

  // Determine right axis label
  const rightAxisLabel = activeMetrics.has('instructions') && activeMetrics.has('relative')
    ? 'k instr / \u00D7'
    : activeMetrics.has('instructions') ? 'k instr' : '\u00D7';

  return (
    <Line
      data={{
        labels: series.map((s) => s.sha),
        datasets,
      }}
      options={{
        responsive: true,
        maintainAspectRatio: false,
        animation: { duration: 250 },
        interaction: {
          mode: 'index',
          intersect: false,
        },
        onClick: (event, _elements, chart) => {
          const axisIdx = isXAxisClick(chart, event);
          if (axisIdx != null) {
            const url = series[axisIdx]?.commitUrl;
            if (url) {
              const native = event.native as any;
              if (native) native.__xAxisHandled = true;
              window.open(url, '_blank');
            }
          }
        },
        onHover: (event, _elements, chart) => {
          const axisIdx = isXAxisClick(chart, event);
          chart.canvas.style.cursor = axisIdx != null ? 'pointer' : '';
        },
        plugins: {
          legend: { display: datasets.length > 1 },
          tooltip: large ? {
            enabled: false,
            external: externalTooltip,
          } : {
            mode: 'index',
            intersect: false,
            titleFont: { size: 14, weight: 'bold' as const },
            bodyFont: { size: 13 },
            callbacks: {
              title: (ctx) => series[ctx[0]!.dataIndex]?.msg ?? '',
              label: (ctx) => {
                const dsLabel = ctx.dataset.label ?? '';
                const val = ctx.parsed.y;
                if (val == null) return '';
                if (dsLabel === METRIC_LABELS.relative) return `${dsLabel}: ${val.toFixed(3)}\u00D7`;
                if (dsLabel === METRIC_LABELS.instructions) return `${dsLabel}: ${val.toFixed(1)}k instr`;
                return `${dsLabel}: ${val.toFixed(2)} ${unit.l}`;
              },
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
            position: 'left',
            title: {
              display: true,
              text: activeMetrics.has('wallclock') ? unit.l
                : activeMetrics.has('instructions') ? 'k instr'
                : '\u00D7',
              font: { size: 13, weight: 'bold' as const },
            },
            ticks: { font: { size: large ? 12 : 11 } },
            beginAtZero: true,
          },
          ...(needsRightAxis ? {
            y1: {
              position: 'right' as const,
              title: { display: true, text: rightAxisLabel, font: { size: 13, weight: 'bold' as const } },
              ticks: { font: { size: large ? 12 : 11 } },
              grid: { drawOnChartArea: false },
              beginAtZero: true,
            },
          } : {}),
        },
      }}
    />
  );
}
