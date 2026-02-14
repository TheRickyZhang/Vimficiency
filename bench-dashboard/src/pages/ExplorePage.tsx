import { exploreRoute } from '../router';
import { ExploreApp } from '../components/ExploreApp';

export function ExplorePage() {
  const data = exploreRoute.useLoaderData();
  const search = exploreRoute.useSearch();

  return <ExploreApp data={data} initialCase={search.case ?? null} />;
}
