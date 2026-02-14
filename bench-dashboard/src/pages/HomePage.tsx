import { Link } from '@tanstack/react-router';
import { homeRoute, type OptimizerSlug } from '../router';
import { parseName } from '../utils/data';
import { fmtTime } from '../utils/format';

interface Change {
  name: string;
  category: string;
  detail: string;
  optimizer: OptimizerSlug;
  pctChange: number;
  ratio: number;
  prevNs: number;
  currNs: number;
}

const MAX_ITEMS = 5;
const ALERT_RATIO = 1.5;

const OPTIMIZER_LABELS: Record<OptimizerSlug, { title: string; desc: string }> = {
  edit: { title: 'EditOptimizer', desc: 'Edit operation optimization' },
  motion: { title: 'MotionOptimizer', desc: 'Cursor motion optimization' },
  composition: { title: 'CompositionOptimizer', desc: 'Full composition optimization' },
};

function toNs(value: number, unit: string): number {
  if (unit === 's') return value * 1e9;
  if (unit === 'ms') return value * 1e6;
  if (unit === 'us') return value * 1e3;
  return value;
}

export function HomePage() {
  const { optimizers } = homeRoute.useLoaderData();

  const changes: Change[] = [];
  for (const { slug, data } of optimizers) {
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
      changes.push({ name: bench.name, category, detail, optimizer: slug, pctChange, ratio, prevNs, currNs });
    }
  }

  const regressions = changes
    .filter((c) => c.pctChange > 0)
    .sort((a, b) => b.pctChange - a.pctChange)
    .slice(0, MAX_ITEMS);

  const improvements = changes
    .filter((c) => c.pctChange < 0)
    .sort((a, b) => a.pctChange - b.pctChange)
    .slice(0, MAX_ITEMS);

  return (
    <>
      <h1>Vimficiency Benchmarks</h1>
      <p className="subtitle">Performance tracking across commits</p>

      <h2>Optimizers</h2>
      <div style={{
        display: 'grid',
        gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))',
        gap: 16,
      }}>
        {(['edit', 'motion', 'composition'] as const).map((slug) => (
          <Link
            key={slug}
            to="/$optimizer"
            params={{ optimizer: slug }}
            search={{ cat: undefined, bench: undefined }}
            style={{
              display: 'block', padding: 24, background: 'white',
              border: '1px solid #e0e0e0', borderRadius: 8,
              textDecoration: 'none', color: 'inherit',
              transition: 'border-color 0.15s, box-shadow 0.15s',
            }}
            onMouseOver={(e) => {
              e.currentTarget.style.borderColor = '#4285f4';
              e.currentTarget.style.boxShadow = '0 2px 8px rgba(66, 133, 244, 0.15)';
            }}
            onMouseOut={(e) => {
              e.currentTarget.style.borderColor = '#e0e0e0';
              e.currentTarget.style.boxShadow = 'none';
            }}
          >
            <h3 style={{ fontSize: '1.25rem', fontWeight: 700, marginBottom: 4, color: 'inherit' }}>
              {OPTIMIZER_LABELS[slug].title}
            </h3>
            <p style={{ color: '#666', fontSize: '0.95rem' }}>
              {OPTIMIZER_LABELS[slug].desc}
            </p>
          </Link>
        ))}
      </div>

      {(regressions.length > 0 || improvements.length > 0) && (
        <div style={{ marginTop: 40 }}>
          <div style={{
            display: 'grid',
            gridTemplateColumns: '1fr 1fr',
            gap: 24,
          }}>
            {regressions.length > 0 && (
              <div>
                <h3>Largest Regressions</h3>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                  {regressions.map((c) => (
                    <ChangeItem key={`${c.optimizer}-${c.name}`} change={c} />
                  ))}
                </div>
              </div>
            )}
            {improvements.length > 0 && (
              <div>
                <h3>Largest Improvements</h3>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
                  {improvements.map((c) => (
                    <ChangeItem key={`${c.optimizer}-${c.name}`} change={c} />
                  ))}
                </div>
              </div>
            )}
          </div>
        </div>
      )}
    </>
  );
}

function ChangeItem({ change: c }: { change: Change }) {
  const overThreshold = c.ratio >= ALERT_RATIO || c.ratio <= 1 / ALERT_RATIO;
  const sign = c.pctChange >= 0 ? '+' : '';
  const label = c.optimizer.charAt(0).toUpperCase() + c.optimizer.slice(1);

  return (
    <Link
      to="/$optimizer"
      params={{ optimizer: c.optimizer }}
      search={{ cat: c.category, bench: c.name }}
      style={{
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '8px 12px', background: 'white',
        border: '1px solid #e8e8e8', borderRadius: 6,
        textDecoration: 'none', color: 'inherit',
        transition: 'border-color 0.15s, box-shadow 0.15s',
      }}
      onMouseOver={(e) => {
        e.currentTarget.style.borderColor = '#4285f4';
        e.currentTarget.style.boxShadow = '0 1px 4px rgba(66, 133, 244, 0.12)';
      }}
      onMouseOut={(e) => {
        e.currentTarget.style.borderColor = '#e8e8e8';
        e.currentTarget.style.boxShadow = 'none';
      }}
    >
      <div style={{ display: 'flex', flexDirection: 'column', gap: 1, minWidth: 0 }}>
        <span style={{ fontWeight: 600, fontSize: '0.9rem', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
          {c.detail}
        </span>
        <span style={{ fontSize: '0.75rem', color: '#999' }}>
          {label} / {c.category}
        </span>
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexShrink: 0 }}>
        {overThreshold && (
          <span style={{ fontSize: '1.1rem', color: '#f9a825' }} title="Exceeds alert threshold">
            {'\u26A0'}
          </span>
        )}
        <span style={{ fontSize: '0.8rem', color: '#888' }}>
          {fmtTime(c.prevNs)} {'\u2192'} {fmtTime(c.currNs)}
        </span>
        <span style={{
          fontWeight: 700, fontSize: '0.9rem', fontVariantNumeric: 'tabular-nums',
          color: c.pctChange > 0 ? '#ea4335' : '#34a853',
        }}>
          {sign}{c.pctChange.toFixed(1)}%
        </span>
      </div>
    </Link>
  );
}
