# CI/CD and Benchmark Dashboard

## CI Workflow (`.github/workflows/bench.yml`)

The single workflow file defines three jobs, all running on `ubuntu-latest`:

### Job 1: `test` (every push and PR)

1. Installs gcc-14, g++-14, and neovim
2. Builds the project in Release mode (`-DVIMFICIENCY_DEBUG=OFF`)
3. Runs unit tests: `./build/tests/vimficiency_tests --gtest_brief=1`

### Job 2: `benchmark-run` (main branch only, after `test` passes)

1. Builds the project in Release mode (same as `test`, but no neovim needed)
2. Runs three benchmark suites with deterministic seeds (`VIMFICIENCY_SEED_MODE=fixed`):
   - `EditOpt.*` → `edit_result.json`
   - `MotionOpt.*` → `motion_result.json`
   - `CompositionOpt.*` → `composition_result.json`
3. Collects exploration data via `vimficiency_explore`
4. Builds and runs baseline benchmarks against HEAD~1 for comparison
5. Uploads all JSON files as a `benchmark-results` artifact

### Job 3: `benchmark-store` (main branch only, after `benchmark-run`)

1. Downloads benchmark artifact
2. Compares current vs baseline results (`scripts/bench-compare.ts`)
3. Builds the dashboard site (`bench-dashboard/`) with Bun + Vite
4. Deploys to `gh-pages`:
   - Ingests benchmark results into `data.json` using `scripts/bench-data.js ingest`
   - Merges exploration data into `explore.json` (keeps last 5 entries)
   - Prunes benchmark data to last 100 entries per suite
   - Copies dashboard HTML/JS/CSS assets

## Data Pipeline

### Benchmark data (`data.json`)

Google Benchmark outputs JSON → `scripts/bench-data.js ingest` parses it and appends to `bench/{optimizer}/data.json` on gh-pages. The dashboard fetches this JSON directly.

**Format:**
```json
{
  "lastUpdate": 1704240000000,
  "repoUrl": "https://github.com/owner/repo",
  "entries": {
    "EditOpt": [
      {
        "commit": { "id": "...", "message": "...", "timestamp": "...", "url": "...", "author": { "username": "..." } },
        "date": 1704240000000,
        "benches": [
          { "name": "EditOpt/BufferSize/1/iterations:5", "value": 1.693, "unit": "ms/iter" }
        ]
      }
    ]
  }
}
```

### Exploration data (`explore.json`)

`vimficiency_explore` outputs JSON → CI merges it into `bench/{optimizer}/explore.json` on gh-pages. The dashboard fetches this JSON directly.

### Migration from legacy format

The `bench-data.js` script automatically migrates from the old `data.js` format (`window.BENCHMARK_DATA = {...}`) to plain `data.json` on first run. Similarly for `explore.js` → `explore.json`.

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
├── vite.config.ts          # base: '/bench/', entries: optimizer.html, explore.html, index.html
├── tsconfig.json
├── optimizer.html           # Vite entry — React app for benchmark charts
├── explore.html             # Vite entry — React app for search space visualization
├── public/
│   ├── index.html           # Static landing page (copied to gh-pages root)
│   ├── data.json            # Dev fixture with sample benchmark data
│   └── explore.json         # Dev fixture with sample exploration data
└── src/
    ├── pages/
    │   ├── optimizer.tsx     # Entry: fetches data.json, renders App
    │   └── explore.tsx       # Entry: fetches explore.json, renders ExploreApp
    ├── components/           # App, ExploreApp, BenchmarkChart, CategorySection, etc.
    ├── types/                # benchmark.d.ts, exploration.d.ts
    └── utils/                # data parsing, formatting, github API
```

### How data flows
- In production, `data.json` and `explore.json` are plain JSON files on gh-pages
- Entry points (`optimizer.tsx`, `explore.tsx`) fetch and parse them, then pass typed data as props to components
- No global variables, no `window.*` access, no `declare global`
- In dev, `public/data.json` and `public/explore.json` provide sample fixture data

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
    │   ├── data.json       # Benchmark data (written by bench-data.js ingest)
    │   ├── explore.json    # Exploration data (written by CI deploy step)
    │   └── explore/
    │       └── index.html  # Explore page (from explore.html build)
    ├── motion/
    │   ├── index.html
    │   ├── data.json
    │   ├── explore.json
    │   └── explore/
    │       └── index.html
    └── composition/
        ├── index.html
        ├── data.json
        ├── explore.json
        └── explore/
            └── index.html
```

## Gitignore

`bench-dashboard/node_modules/` and `bench-dashboard/dist/` are gitignored. Source files, `bun.lock`, and `public/` are tracked.
