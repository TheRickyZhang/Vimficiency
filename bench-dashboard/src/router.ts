import { createRootRoute, createRoute, createRouter, redirect, Outlet } from '@tanstack/react-router';
import type { BenchmarkData } from './types/benchmark';
import type { ExplorationData } from './types/exploration';
import { loadBenchmarkData, discoverCategories } from './utils/data';
import { RootLayout } from './components/RootLayout';
import { HomePage } from './pages/HomePage';
import { OptimizerPage } from './pages/OptimizerPage';
import { ExplorePage } from './pages/ExplorePage';

const VALID_OPTIMIZERS = ['edit', 'motion', 'composition'] as const;
export type OptimizerSlug = (typeof VALID_OPTIMIZERS)[number];

const base = import.meta.env.BASE_URL; // '/Vimficiency/'

export const rootRoute = createRootRoute({
  component: RootLayout,
});

export interface HomeLoaderData {
  optimizers: {
    slug: OptimizerSlug;
    data: BenchmarkData;
  }[];
}

export const homeRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  loader: async (): Promise<HomeLoaderData> => {
    const results = await Promise.all(
      VALID_OPTIMIZERS.map(async (slug) => {
        try {
          const res = await fetch(`${base}${slug}/data.json?_=${Date.now()}`);
          if (!res.ok) return null;
          const data: BenchmarkData = await res.json();
          return { slug, data };
        } catch {
          return null;
        }
      }),
    );
    return { optimizers: results.filter((r) => r !== null) };
  },
  component: HomePage,
});

const optimizerLayoutRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '$optimizer',
  beforeLoad: ({ params }) => {
    if (!(VALID_OPTIMIZERS as readonly string[]).includes(params.optimizer)) {
      throw redirect({ to: '/' });
    }
  },
  component: Outlet,
});

export interface OptimizerLoaderData {
  data: ReturnType<typeof loadBenchmarkData>;
  categories: ReturnType<typeof discoverCategories>;
  optimizerName: string;
  repoUrl: string;
}

export const optimizerIndexRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/',
  loader: async ({ params }): Promise<OptimizerLoaderData> => {
    const res = await fetch(`${base}${params.optimizer}/data.json?_=${Date.now()}`);
    const raw: BenchmarkData = await res.json();
    const data = loadBenchmarkData(raw);
    const categories = discoverCategories(data);
    let optimizerName = 'Benchmarks';
    const firstName = data[data.length - 1]?.benches[0]?.name;
    if (firstName) {
      optimizerName = firstName.split('/')[0] ?? 'Benchmarks';
    }
    return { data, categories, optimizerName, repoUrl: raw.repoUrl };
  },
  validateSearch: (search: Record<string, unknown>) => ({
    cat: (search['cat'] as string) || undefined,
    bench: (search['bench'] as string) || undefined,
  }),
  component: OptimizerPage,
});

export const exploreRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/explore',
  loader: async ({ params }): Promise<ExplorationData> => {
    const res = await fetch(`${base}${params.optimizer}/explore.json?_=${Date.now()}`);
    return await res.json();
  },
  validateSearch: (search: Record<string, unknown>) => ({
    case: (search['case'] as string) || undefined,
  }),
  component: ExplorePage,
});

const routeTree = rootRoute.addChildren([
  homeRoute,
  optimizerLayoutRoute.addChildren([optimizerIndexRoute, exploreRoute]),
]);

export const router = createRouter({
  routeTree,
  basepath: base.replace(/\/$/, ''), // '/Vimficiency'
  defaultPreload: 'intent',
});

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router;
  }
}
