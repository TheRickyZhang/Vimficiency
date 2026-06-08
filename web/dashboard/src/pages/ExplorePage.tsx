import { exploreRoute } from '../router';
import { ExploreApp } from '../components/ExploreApp';

export function ExplorePage() {
  const data = exploreRoute.useLoaderData();
  const search = exploreRoute.useSearch();

  if (!data) {
    return (
      <>
        <h1>Search Space Explorer</h1>
        <p className="subtitle">No exploration data available. Run benchmarks to populate.</p>
      </>
    );
  }

  return <ExploreApp data={data} initialCase={search.case ?? null} />;
}
