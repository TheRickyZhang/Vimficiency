import type { CoverageBinary } from '../types/coverage';

export function CoverageView({ coverage }: { coverage: CoverageBinary | null }) {
  if (!coverage || coverage.suites.length === 0) {
    return <p className="subtitle">No coverage data available. Run the suite to populate.</p>;
  }

  const { totalCases, failures, suites } = coverage;

  return (
    <>
      <div className="flex flex-wrap gap-6 mb-8">
        <Stat value={`${suites.length}`} label={`suite${suites.length !== 1 ? 's' : ''}`} />
        <Stat value={`${totalCases}`} label={`case${totalCases !== 1 ? 's' : ''}`} />
        <Stat
          value={failures === 0 ? 'all passing' : `${failures} failing`}
          label="status"
          tone={failures === 0 ? 'good' : 'bad'}
        />
      </div>

      <div className="grid grid-cols-[repeat(auto-fill,minmax(280px,1fr))] gap-4">
        {suites.map((suite) => (
          <section key={suite.name} className="card p-4">
            <div className="flex items-baseline justify-between gap-3 mb-3">
              <h4 className="m-0 text-base font-bold text-[#333] truncate">{suite.name}</h4>
              <span className="text-xs text-muted whitespace-nowrap">
                {suite.cases.length} case{suite.cases.length !== 1 ? 's' : ''}
              </span>
            </div>
            <ul className="list-none m-0 p-0 flex flex-col gap-1">
              {suite.cases.map((c) => (
                <li key={c.name} className="flex items-center gap-2 text-sm">
                  <span className={c.passed ? 'text-good' : 'text-bad'}>{c.passed ? '✓' : '✗'}</span>
                  <span className="text-[#333] truncate" title={c.name}>{c.name}</span>
                </li>
              ))}
            </ul>
          </section>
        ))}
      </div>
    </>
  );
}

function Stat({ value, label, tone }: { value: string; label: string; tone?: 'good' | 'bad' }) {
  const toneClass = tone === 'good' ? 'text-good' : tone === 'bad' ? 'text-bad' : 'text-[#333]';
  return (
    <div>
      <div className={`text-2xl font-extrabold ${toneClass}`}>{value}</div>
      <div className="text-[0.9rem] text-muted mt-1">{label}</div>
    </div>
  );
}
