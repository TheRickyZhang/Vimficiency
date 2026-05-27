import { useState } from 'react';
import { useNavigate } from '@tanstack/react-router';
import { testSuiteRoute } from '../router';
import { App } from '../components/App';
import { approvalFixturesForCategory, testSuiteDescription, type ApprovalFixture, type TestSuiteSlug } from '../utils/testSuites';
import { approvalFixtureContents } from '../utils/approvalFixtureContents';

export function TestSuitePage() {
  const { data, categories, optimizerName, repoUrl } = testSuiteRoute.useLoaderData();
  const { cat, bench } = testSuiteRoute.useSearch();
  const { optimizer, testSuite } = testSuiteRoute.useParams();
  const suiteSlug = testSuite as TestSuiteSlug;
  const navigate = useNavigate({ from: testSuiteRoute.fullPath });
  const [fixtureCategory, setFixtureCategory] = useState<string | null>(null);

  if (!data) {
    return (
      <>
        <h1>{optimizerName}</h1>
        <p className="subtitle">No test timing data available. Run benchmarks to populate.</p>
      </>
    );
  }

  const latestRun = data[data.length - 1];
  const fixtures = fixtureCategory
    ? approvalFixturesForCategory(
      fixtureCategory,
      categories[fixtureCategory] ?? [],
      repoUrl,
      latestRun,
    )
    : [];
  const inlinePanel = fixtureCategory
    ? (
      <ApprovalFixturesPanel
        category={fixtureCategory}
        fixtures={fixtures}
        onClose={() => setFixtureCategory(null)}
      />
    )
    : undefined;

  return (
    <>
      <App
        data={data}
        categories={categories}
        optimizerName={optimizerName}
        optimizer={optimizer}
        repoUrl={repoUrl}
        activeCategory={cat ?? null}
        openBench={bench ?? null}
        showExplore={false}
        subtitle={testSuiteDescription(suiteSlug)}
        inlinePanel={inlinePanel}
        categoryActionLabel={suiteSlug === 'approval' ? 'Explore' : undefined}
        onCategoryAction={suiteSlug === 'approval'
          ? (category) => setFixtureCategory((current) => current === category ? null : category)
          : undefined}
        onNavigate={(newCat, benchName) => {
          void navigate({
            search: { cat: newCat || undefined, bench: benchName || undefined },
          });
          window.scrollTo(0, 0);
        }}
        onBenchConsumed={() => {
          void navigate({
            search: { cat, bench: undefined },
            replace: true,
          });
        }}
      />
    </>
  );
}

function ApprovalFixturesPanel({
  category,
  fixtures,
  onClose,
}: {
  category: string;
  fixtures: ApprovalFixture[];
  onClose: () => void;
}) {
  return (
    <section className="card p-5 mb-8">
      <PanelHeader
        title={`${category} Fixtures`}
        description="Approved snapshot files matched by this approval test file."
        onClose={onClose}
      />

      {fixtures.length === 0 ? (
        <p className="text-left text-muted">No approved fixtures were found in the latest timing data.</p>
      ) : (
        <div className="grid gap-4">
          {fixtures.map((fixture) => (
            <div key={fixture.path} className="rounded-lg border border-border-light overflow-hidden bg-[#fcfcfc]">
              <div className="flex items-start justify-between gap-4 px-4 py-3 border-b border-border-light">
                <div className="min-w-0">
                  <div className="font-semibold text-[#333] truncate">{fixture.label}</div>
                  <div className="text-xs text-muted mt-1 break-all">{fixture.path}</div>
                </div>
                {fixture.href && (
                  <a href={fixture.href} className="link-brand text-sm whitespace-nowrap">Source</a>
                )}
              </div>
              {approvalFixtureContents[fixture.path] ? (
                <pre className="m-0 p-4 max-h-[360px] overflow-auto text-left text-[0.82rem] leading-relaxed bg-[#f7f7f7] text-[#222] whitespace-pre-wrap break-words">{approvalFixtureContents[fixture.path]}</pre>
              ) : (
                <p className="text-left text-sm text-muted px-4 py-3 m-0">
                  This fixture is present in timing data, but its text is not bundled in the dashboard.
                </p>
              )}
            </div>
          ))}
        </div>
      )}
    </section>
  );
}

function PanelHeader({
  title,
  description,
  onClose,
}: {
  title: string;
  description: string;
  onClose: () => void;
}) {
  return (
    <div className="flex items-start justify-between gap-4 mb-4">
      <div>
        <h2 className="text-left mb-1">{title}</h2>
        <p className="text-left text-sm text-muted m-0">{description}</p>
      </div>
      <button type="button" className="explore-btn" onClick={onClose}>Close</button>
    </div>
  );
}
