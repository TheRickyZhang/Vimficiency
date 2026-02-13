import { fmtTime } from '../utils/format';
import { parseName } from '../utils/data';

interface BenchEntry {
  name: string;
  value: number;
  unit: string;
}

interface BenchRun {
  commit: { id: string };
  benches: BenchEntry[];
}

interface BenchData {
  entries: Record<string, BenchRun[]>;
}

interface Change {
  name: string;
  category: string;
  detail: string;
  optimizer: string;
  pctChange: number;
  ratio: number;
  prevNs: number;
  currNs: number;
}

const OPTIMIZERS = ['edit', 'motion', 'composition'] as const;
const MAX_ITEMS = 5;
const ALERT_RATIO = 1.5;

async function fetchData(optimizer: string): Promise<BenchData | null> {
  try {
    const resp = await fetch(`bench/${optimizer}/data.js`);
    if (!resp.ok) return null;
    const text = await resp.text();
    const json = text.replace(/^window\.BENCHMARK_DATA\s*=\s*/, '').replace(/;\s*$/, '');
    return JSON.parse(json);
  } catch {
    return null;
  }
}

function toNs(value: number, unit: string): number {
  if (unit === 's') return value * 1e9;
  if (unit === 'ms') return value * 1e6;
  if (unit === 'us') return value * 1e3;
  return value;
}

async function loadChanges(): Promise<Change[]> {
  const changes: Change[] = [];

  for (const opt of OPTIMIZERS) {
    const data = await fetchData(opt);
    if (!data) continue;

    const runs = Object.values(data.entries)[0];
    if (!runs || runs.length < 2) continue;

    const latest = runs[runs.length - 1]!;
    const prev = runs[runs.length - 2]!;

    const prevMap = new Map(prev.benches.map((b) => [b.name, b]));

    for (const bench of latest.benches) {
      const prevBench = prevMap.get(bench.name);
      if (!prevBench) continue;

      const currNs = toNs(bench.value, bench.unit);
      const prevNs = toNs(prevBench.value, prevBench.unit);
      if (prevNs === 0) continue;

      const ratio = currNs / prevNs;
      const pctChange = (ratio - 1) * 100;
      if (Math.abs(pctChange) < 1) continue;
      const { category, detail } = parseName(bench.name);

      changes.push({ name: bench.name, category, detail, optimizer: opt, pctChange, ratio, prevNs, currNs });
    }
  }

  return changes;
}

function optimizerLabel(opt: string): string {
  return opt.charAt(0).toUpperCase() + opt.slice(1);
}

function renderItem(c: Change): HTMLAnchorElement {
  const item = document.createElement('a');
  item.className = 'change-item';
  item.href = `bench/${c.optimizer}/?bench=${encodeURIComponent(c.name)}#${encodeURIComponent(c.category)}`;

  const left = document.createElement('div');
  left.className = 'change-left';
  const detail = document.createElement('span');
  detail.className = 'change-detail';
  detail.textContent = c.detail;
  const meta = document.createElement('span');
  meta.className = 'change-meta';
  meta.textContent = `${optimizerLabel(c.optimizer)} / ${c.category}`;
  left.appendChild(detail);
  left.appendChild(meta);

  const right = document.createElement('div');
  right.className = 'change-right';

  const overThreshold = c.ratio >= ALERT_RATIO || c.ratio <= 1 / ALERT_RATIO;
  if (overThreshold) {
    const warn = document.createElement('span');
    warn.className = 'change-warn';
    warn.textContent = '\u26A0';
    warn.title = 'Exceeds alert threshold';
    right.appendChild(warn);
  }

  const times = document.createElement('span');
  times.className = 'change-times';
  times.textContent = `${fmtTime(c.prevNs)} \u2192 ${fmtTime(c.currNs)}`;
  right.appendChild(times);

  const pct = document.createElement('span');
  const sign = c.pctChange >= 0 ? '+' : '';
  pct.textContent = `${sign}${c.pctChange.toFixed(1)}%`;
  pct.className = `change-pct ${c.pctChange > 0 ? 'regression' : 'improvement'}`;
  right.appendChild(pct);

  item.appendChild(left);
  item.appendChild(right);
  return item;
}

function render(changes: Change[]) {
  const regressions = changes
    .filter((c) => c.pctChange > 0)
    .sort((a, b) => b.pctChange - a.pctChange)
    .slice(0, MAX_ITEMS);

  const improvements = changes
    .filter((c) => c.pctChange < 0)
    .sort((a, b) => a.pctChange - b.pctChange)
    .slice(0, MAX_ITEMS);

  if (!regressions.length && !improvements.length) return;

  const section = document.getElementById('changes-section')!;
  section.style.display = 'block';

  if (regressions.length) {
    const col = document.getElementById('regressions-col')!;
    const list = document.getElementById('regressions-list')!;
    col.style.display = 'block';
    for (const c of regressions) list.appendChild(renderItem(c));
  }

  if (improvements.length) {
    const col = document.getElementById('improvements-col')!;
    const list = document.getElementById('improvements-list')!;
    col.style.display = 'block';
    for (const c of improvements) list.appendChild(renderItem(c));
  }
}

loadChanges().then(render);
