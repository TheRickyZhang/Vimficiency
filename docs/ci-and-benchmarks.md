# CI/CD and Benchmark Dashboard

## CI Workflow (`.github/workflows/bench.yml`)

The single workflow file defines three jobs, all running on `ubuntu-latest` with an Arch Linux container:

### Job 1: `test` (every push and PR)

1. Installs `base-devel cmake git neovim` via pacman
2. Builds the project in Release mode (`-DVIMFICIENCY_DEBUG=OFF`)
3. Runs unit tests: `./build/tests/vimficiency_tests --gtest_brief=1`

### Job 2: `benchmark-run` (main branch only, after `test` passes)

1. Builds the project in Release mode (same as `test`, but no neovim needed)
2. Runs three benchmark suites with deterministic seeds (`VIMFICIENCY_SEED_MODE=fixed`):
   - `EditOptimizer.*` → `edit_result.json`
   - `MotionOptimizer.*` → `motion_result.json`
   - `CompositionOptimizer.*` → `composition_result.json`
3. Uploads the three JSON files as a `benchmark-results` artifact

### Job 3: `benchmark-store` (main branch only, after `benchmark-run`)

1. Downloads benchmark artifact
2. Uses [`benchmark-action/github-action-benchmark@v1`](https://github.com/benchmark-action/github-action-benchmark) to store results in the `gh-pages` branch under `bench/{edit,motion,composition}/data.js`
   - Alert threshold: 150% regression
   - Comments on alert, but does not fail the build
3. Builds the dashboard site (`bench-dashboard/`) with Bun + Vite
4. Deploys the built dashboard to `gh-pages`:
   - Root `index.html` → landing page linking to each optimizer
   - `bench/{edit,motion,composition}/index.html` → per-optimizer chart pages
   - `bench/assets/` → shared JS/CSS bundles
   - Each optimizer dir also has `data.js` (written by benchmark-action, not the dashboard build)

## Benchmark Dashboard (`bench-dashboard/`)

A React + TypeScript + Vite app that renders benchmark history charts.

### Stack
- **Runtime/package manager:** Bun
- **Framework:** React 19, TypeScript 5.7
- **Build:** Vite 6 with `@vitejs/plugin-react`
- **Charts:** Chart.js + react-chartjs-2

### Structure
```
bench-dashboard/
├── package.json
├── bun.lock
├── vite.config.ts          # base: '/bench/', single entry: optimizer.html
├── tsconfig.json
├── optimizer.html           # Vite entry — loads data.js then React app
├── public/
│   ├── index.html           # Static landing page (copied to gh-pages root)
│   └── data.js              # Dev fixture with sample benchmark data
└── src/
    ├── pages/optimizer.tsx   # Entry point for per-optimizer page
    ├── components/           # App, BenchmarkChart, CategorySection, ChartModal, etc.
    ├── hooks/                # useBenchmarkData
    ├── types/                # benchmark.d.ts
    └── utils/                # data parsing, formatting
```

### How data flows
- In production, `benchmark-action` writes a `data.js` file into each `bench/{optimizer}/` directory on `gh-pages`. This script sets `window.BENCHMARK_DATA` with the full commit history.
- The React app reads `window.BENCHMARK_DATA` at runtime and renders charts.
- In dev, `public/data.js` provides sample fixture data.

### Local development
```bash
# Install dependencies
cd bench-dashboard && bun install

# Dev server with hot reload
bun run dev

# Production build (outputs to bench-dashboard/dist/)
bun run build

# Preview production build
bun run preview
```

### Verifying the CI build locally
```bash
cd bench-dashboard && bun install --frozen-lockfile && bun run build
```
This mirrors exactly what CI runs. If this succeeds, the CI dashboard step will too.

## gh-pages Branch Layout

```
gh-pages/
├── index.html              # Landing page (from bench-dashboard/public/index.html)
├── .nojekyll               # Disables Jekyll processing
└── bench/
    ├── assets/             # Vite-built JS/CSS bundles
    ├── edit/
    │   ├── index.html      # Optimizer page (from optimizer.html build)
    │   └── data.js         # Benchmark data (written by benchmark-action)
    ├── motion/
    │   ├── index.html
    │   └── data.js
    └── composition/
        ├── index.html
        └── data.js
```

## Gitignore

`bench-dashboard/node_modules/` and `bench-dashboard/dist/` are gitignored. Source files, `bun.lock`, and `public/` are tracked.
