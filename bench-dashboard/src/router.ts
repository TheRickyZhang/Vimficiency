import { createRootRoute, createRoute, createRouter, redirect, Outlet } from '@tanstack/react-router';
import type { BenchmarkData } from './types/benchmark';
import type { ExplorationData } from './types/exploration';
import { loadBenchmarkData, discoverCategories } from './utils/data';
import { RootLayout } from './components/RootLayout';
import { HomePage } from './pages/HomePage';
import { OptimizerPage } from './pages/OptimizerPage';
import { TestSuitePage } from './pages/TestSuitePage';
import { ExplorePage } from './pages/ExplorePage';
import { ChunkDetailPage } from './pages/ChunkDetailPage';
import { isTestSuiteSlug, loadTestSuiteBenchmarkData } from './utils/testSuites';

const VALID_OPTIMIZERS = ['edit', 'motion', 'composition', 'tests'] as const;
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
  data: ReturnType<typeof loadBenchmarkData> | null;
  categories: ReturnType<typeof discoverCategories>;
  optimizerName: string;
  repoUrl: string;
}

export const optimizerIndexRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/',
  loader: async ({ params }): Promise<OptimizerLoaderData> => {
    try {
      const res = await fetch(`${base}${params.optimizer}/data.json?_=${Date.now()}`);
      if (!res.ok) return { data: null, categories: {}, optimizerName: 'Benchmarks', repoUrl: '' };
      const raw: BenchmarkData = await res.json();
      const data = params.optimizer === 'tests'
        ? loadTestSuiteBenchmarkData(raw, 'unit')
        : loadBenchmarkData(raw);
      const categories = discoverCategories(data);
      let optimizerName = 'Benchmarks';
      const firstName = data[data.length - 1]?.benches[0]?.name;
      if (params.optimizer === 'tests') {
        optimizerName = 'Unit Tests';
      } else if (firstName) {
        optimizerName = firstName.split('/')[0] ?? 'Benchmarks';
      }
      return { data: data.length > 0 ? data : null, categories, optimizerName, repoUrl: raw.repoUrl };
    } catch {
      return { data: null, categories: {}, optimizerName: 'Benchmarks', repoUrl: '' };
    }
  },
  validateSearch: (search: Record<string, unknown>) => ({
    cat: (search['cat'] as string) || undefined,
    bench: (search['bench'] as string) || undefined,
  }),
  component: OptimizerPage,
});

export const testSuiteRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '$testSuite',
  beforeLoad: ({ params }) => {
    if (params.optimizer !== 'tests' || !isTestSuiteSlug(params.testSuite)) {
      throw redirect({ to: '/' });
    }
  },
  loader: async ({ params }): Promise<OptimizerLoaderData> => {
    if (!isTestSuiteSlug(params.testSuite)) {
      return { data: null, categories: {}, optimizerName: 'Tests', repoUrl: '' };
    }

    try {
      const res = await fetch(`${base}tests/data.json?_=${Date.now()}`);
      if (!res.ok) return { data: null, categories: {}, optimizerName: 'Tests', repoUrl: '' };
      const raw: BenchmarkData = await res.json();
      const data = loadTestSuiteBenchmarkData(raw, params.testSuite);
      const categories = discoverCategories(data);
      const firstName = data[data.length - 1]?.benches[0]?.name;
      const optimizerName = firstName ? `${firstName.split('/')[0]} Tests` : 'Tests';
      return { data: data.length > 0 ? data : null, categories, optimizerName, repoUrl: raw.repoUrl };
    } catch {
      return { data: null, categories: {}, optimizerName: 'Tests', repoUrl: '' };
    }
  },
  validateSearch: (search: Record<string, unknown>) => ({
    cat: (search['cat'] as string) || undefined,
    bench: (search['bench'] as string) || undefined,
  }),
  component: TestSuitePage,
});

export const exploreRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/explore',
  beforeLoad: ({ params }) => {
    if (params.optimizer === 'tests') {
      throw redirect({ to: '/$optimizer', params: { optimizer: 'tests' }, search: { cat: undefined, bench: undefined } });
    }
  },
  loader: async ({ params }): Promise<ExplorationData | null> => {
    try {
      const res = await fetch(`${base}${params.optimizer}/explore.json?_=${Date.now()}`);
      if (!res.ok) return null;
      return await res.json();
    } catch {
      return null;
    }
  },
  validateSearch: (search: Record<string, unknown>) => ({
    case: (search['case'] as string) || undefined,
  }),
  component: ExplorePage,
});

export const chunkDetailRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/explore/chunk',
  beforeLoad: ({ params }) => {
    if (params.optimizer === 'tests') {
      throw redirect({ to: '/$optimizer', params: { optimizer: 'tests' }, search: { cat: undefined, bench: undefined } });
    }
  },
  loader: async ({ params }): Promise<ExplorationData | null> => {
    try {
      const res = await fetch(`${base}${params.optimizer}/explore.json?_=${Date.now()}`);
      if (!res.ok) return null;
      return await res.json();
    } catch {
      return null;
    }
  },
  validateSearch: (search: Record<string, unknown>) => ({
    case: (search['case'] as string) || undefined,
    chunkIndex: search['chunkIndex'] != null ? Number(search['chunkIndex']) : undefined,
  }),
  component: ChunkDetailPage,
});

const routeTree = rootRoute.addChildren([
  homeRoute,
  optimizerLayoutRoute.addChildren([optimizerIndexRoute, chunkDetailRoute, exploreRoute, testSuiteRoute]),
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
