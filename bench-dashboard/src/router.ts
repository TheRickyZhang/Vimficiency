import { createRootRoute, createRoute, createRouter, redirect, Outlet } from '@tanstack/react-router';
import type { BenchmarkData, BenchmarkRun } from './types/benchmark';
import type { ExplorationData } from './types/exploration';
import type { CoverageData, CoverageBinary } from './types/coverage';
import { loadBenchmarkData, discoverCategories, type TimePoint } from './utils/data';
import { exploreCategoryNames } from './utils/exploration';
import { RootLayout } from './components/RootLayout';
import { HomePage } from './pages/HomePage';
import { OptimizerPage } from './pages/OptimizerPage';
import { TestSuitePage } from './pages/TestSuitePage';
import { ExplorePage } from './pages/ExplorePage';
import { ChunkDetailPage } from './pages/ChunkDetailPage';
import {
  isTestSuiteSlug,
  loadTestSuiteBenchmarkData,
  binaryTotalSeries,
  testSuiteKind,
  getTestSuite,
  type TestSuiteKind,
} from './utils/testSuites';

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
  coverage: CoverageData | null;
}

export const homeRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  loader: async (): Promise<HomeLoaderData> => {
    const [results, coverage] = await Promise.all([
      Promise.all(
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
      ),
      (async (): Promise<CoverageData | null> => {
        try {
          const res = await fetch(`${base}tests/coverage.json?_=${Date.now()}`);
          if (!res.ok) return null;
          return await res.json();
        } catch {
          return null;
        }
      })(),
    ]);
    return { optimizers: results.filter((r) => r !== null), coverage };
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
  // Categories that actually have exploration data, for gating Explore buttons.
  // null = unknown (explore.json missing/failed): don't gate, preserve old behavior.
  exploreCategories: string[] | null;
}

function emptyOptimizerData(): OptimizerLoaderData {
  return { data: null, categories: {}, optimizerName: 'Benchmarks', repoUrl: '', exploreCategories: null };
}

async function loadExploreCategories(optimizer: string): Promise<string[] | null> {
  try {
    const res = await fetch(`${base}${optimizer}/explore.json?_=${Date.now()}`);
    if (!res.ok) return null;
    return exploreCategoryNames(await res.json());
  } catch {
    return null;
  }
}

export const optimizerIndexRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '/',
  loader: async ({ params }): Promise<OptimizerLoaderData> => {
    const isTests = params.optimizer === 'tests';
    try {
      const [res, exploreCategories] = await Promise.all([
        fetch(`${base}${params.optimizer}/data.json?_=${Date.now()}`),
        isTests ? Promise.resolve<string[] | null>(null) : loadExploreCategories(params.optimizer),
      ]);
      if (!res.ok) return emptyOptimizerData();
      const raw: BenchmarkData = await res.json();
      const data = isTests
        ? loadTestSuiteBenchmarkData(raw, 'unit')
        : loadBenchmarkData(raw);
      const categories = discoverCategories(data);
      let optimizerName = 'Benchmarks';
      const firstName = data[data.length - 1]?.benches[0]?.name;
      if (isTests) {
        optimizerName = 'Unit Tests';
      } else if (firstName) {
        optimizerName = firstName.split('/')[0] ?? 'Benchmarks';
      }
      return { data: data.length > 0 ? data : null, categories, optimizerName, repoUrl: raw.repoUrl, exploreCategories };
    } catch {
      return emptyOptimizerData();
    }
  },
  validateSearch: (search: Record<string, unknown>) => ({
    cat: (search['cat'] as string) || undefined,
    bench: (search['bench'] as string) || undefined,
  }),
  component: OptimizerPage,
});

export interface TestSuiteLoaderData {
  kind: TestSuiteKind;
  optimizerName: string;
  repoUrl: string;
  data: BenchmarkRun[] | null;
  categories: Record<string, string[]>;
  binaryTotal: TimePoint[];
  coverage: CoverageBinary | null;
}

function emptyTestSuiteData(kind: TestSuiteKind, optimizerName: string): TestSuiteLoaderData {
  return { kind, optimizerName, repoUrl: '', data: null, categories: {}, binaryTotal: [], coverage: null };
}

export const testSuiteRoute = createRoute({
  getParentRoute: () => optimizerLayoutRoute,
  path: '$testSuite',
  beforeLoad: ({ params }) => {
    if (params.optimizer !== 'tests' || !isTestSuiteSlug(params.testSuite)) {
      throw redirect({ to: '/' });
    }
  },
  loader: async ({ params }): Promise<TestSuiteLoaderData> => {
    const slug = params.testSuite;
    if (!isTestSuiteSlug(slug)) return emptyTestSuiteData('timing', 'Tests');

    const kind = testSuiteKind(slug);
    const optimizerName = getTestSuite(slug).title;

    try {
      if (kind === 'coverage') {
        const res = await fetch(`${base}tests/coverage.json?_=${Date.now()}`);
        if (!res.ok) return emptyTestSuiteData(kind, optimizerName);
        const cov: CoverageData = await res.json();
        return {
          ...emptyTestSuiteData(kind, optimizerName),
          coverage: cov.binaries[getTestSuite(slug).label] ?? null,
        };
      }

      const res = await fetch(`${base}tests/data.json?_=${Date.now()}`);
      if (!res.ok) return emptyTestSuiteData(kind, optimizerName);
      const raw: BenchmarkData = await res.json();
      const data = loadTestSuiteBenchmarkData(raw, slug);
      return {
        kind,
        optimizerName,
        repoUrl: raw.repoUrl,
        data: data.length > 0 ? data : null,
        categories: discoverCategories(data),
        binaryTotal: binaryTotalSeries(raw, slug, raw.repoUrl),
        coverage: null,
      };
    } catch {
      return emptyTestSuiteData(kind, optimizerName);
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
