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
  prevNs: number;
  currNs: number;
}

const OPTIMIZERS = ['edit', 'motion', 'composition'] as const;

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

      const pctChange = ((currNs - prevNs) / prevNs) * 100;
      const { category, detail } = parseName(bench.name);

      changes.push({ name: bench.name, category, detail, optimizer: opt, pctChange, prevNs, currNs });
    }
  }

  changes.sort((a, b) => Math.abs(b.pctChange) - Math.abs(a.pctChange));
  return changes.slice(0, 5);
}

function render(changes: Change[]) {
  if (changes.length === 0) return;

  const container = document.getElementById('top-changes')!;
  const list = document.getElementById('change-list')!;

  for (const c of changes) {
    const item = document.createElement('div');
    item.className = 'change-item';

    const link = document.createElement('a');
    link.href = `bench/${c.optimizer}/`;
    link.textContent = `${c.category} / ${c.detail}`;

    const pct = document.createElement('span');
    const sign = c.pctChange >= 0 ? '+' : '';
    pct.textContent = `${sign}${c.pctChange.toFixed(1)}%`;
    pct.className = `change-pct ${c.pctChange > 0 ? 'regression' : 'improvement'}`;

    const times = document.createElement('span');
    times.className = 'change-times';
    times.textContent = `${fmtTime(c.prevNs)} \u2192 ${fmtTime(c.currNs)}`;

    item.appendChild(link);
    item.appendChild(pct);
    item.appendChild(times);
    list.appendChild(item);
  }

  container.style.display = 'block';
}

loadChanges().then(render);
