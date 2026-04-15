# CI/CD and Benchmark Dashboard

This page covers the benchmark pipeline. The separate
`.github/workflows/vimdoc.yml` workflow regenerates
`doc/vimficiency.txt` from `doc-src/**` on pushes to `main`; see
[script-usage.md](./script-usage.md) for that pipeline.

## CI Workflow (`.github/workflows/bench.yml`)

The single workflow file defines three jobs, all running on `ubuntu-latest`.

### Dependency setup abstraction

Shared dependency setup is centralized in `.github/actions/setup-ci-deps/action.yml`.

- Always installs `gcc-14` and `g++-14`
- Optionally installs Neovim (version-controlled by workflow env `NEOVIM_VERSION`)
- Caches extracted Neovim under `~/.local/neovim/<version>` with key `neovim-<os>-<version>`
- Adds cached Neovim to `PATH` when enabled

### Job 1: `test` (every push and PR)

1. Runs `setup-ci-deps` with Neovim enabled
2. Restores compiler cache via `hendrikmuhs/ccache-action@v1` (`key: test`)
3. Restores CMake dependency cache (`build/_deps`, `deps-v2-*`)
4. Builds in Release mode (`-DVIMFICIENCY_DEBUG=OFF`)
5. Runs unit tests: `./build/tests/vimficiency_tests --gtest_brief=1`

### Job 2: `benchmark-run` (main branch only, after `test` passes)

1. Runs `setup-ci-deps` without Neovim
2. Restores compiler cache via `hendrikmuhs/ccache-action@v1` (`key: bench`)
3. Restores CMake dependency cache (`build/_deps`, `deps-v2-*`)
4. Builds the project in Release mode
5. Runs three benchmark suites with deterministic seeds (`VIMFICIENCY_SEED_MODE=fixed`):
   - `EditOpt.*` → `edit_result.json`
   - `MotionOpt.*` → `motion_result.json`
   - `CompositionOpt.*` → `composition_result.json`
6. Collects exploration data via `vimficiency_explore`
7. Builds and runs baseline benchmarks against `HEAD~1` for comparison
8. Uploads all JSON files as a `benchmark-results` artifact

### Job 3: `benchmark-store` (main branch only, after `benchmark-run`)

1. Downloads benchmark artifact
2. Compares current vs baseline results (`scripts/bench-compare.ts`)
3. Builds the dashboard site (`bench-dashboard/`) with Bun + Vite
4. Deploys to `gh-pages`:
   - Ingests benchmark results into `data.json` using `scripts/bench-data.ts ingest`
   - Merges exploration data into `explore.json` (keeps last 5 entries)
   - Prunes benchmark data to last 100 entries per suite
   - Copies dashboard HTML/JS/CSS assets

### CI cache and performance notes

- `ccache` cache is restored before build in `test` and `benchmark-run`, reducing repeated compile work.
- `build/_deps` cache stores CMake-fetched third-party source/build artifacts (for this repo, primarily GoogleTest and related CMake external content).
- Neovim install in `test` is cached by OS + version. On cache hit, CI skips the GitHub release download and untar.
- `apt-get update` and apt package installation are intentionally not cached in this workflow. These commands still run on every fresh GitHub-hosted runner, so `Setup CI deps` can remain one of the slower steps even when all project-level caches hit.

## Data Pipeline

### Benchmark data (`data.json`)

Google Benchmark outputs JSON → `scripts/bench-data.ts ingest` parses it and appends to `{optimizer}/data.json` on gh-pages. The dashboard fetches this JSON directly.

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

`vimficiency_explore` outputs JSON → CI merges it into `{optimizer}/explore.json` on gh-pages. The dashboard fetches this JSON directly.

### Migration from legacy format

The `bench-data.ts` script automatically migrates from the old `data.js` format (`window.BENCHMARK_DATA = {...}`) to plain `data.json` on first run. Similarly for `explore.js` → `explore.json`.

## Benchmark Dashboard (`bench-dashboard/`)

A single-page React + TypeScript + Vite app using TanStack Router for client-side navigation.

### Stack
- **Runtime/package manager:** Bun
- **Framework:** React 19, TypeScript 5.7
- **Routing:** TanStack Router (code-based routes)
- **Build:** Vite 6 with `@vitejs/plugin-react`
- **Charts:** Chart.js + react-chartjs-2

### Structure
```
bench-dashboard/
├── package.json
├── bun.lock
├── vite.config.ts          # base: '/Vimficiency/', single entry
├── tsconfig.json
├── index.html              # SPA shell (single Vite entry point)
├── public/
│   ├── edit/               # Dev fixtures per optimizer
│   │   ├── data.json
│   │   └── explore.json
│   ├── motion/
│   │   ├── data.json
│   │   └── explore.json
│   └── composition/
│       ├── data.json
│       └── explore.json
└── src/
    ├── main.tsx            # App entry: Chart.js registration, RouterProvider
    ├── router.ts           # Route tree, loaders, search param validation
    ├── index.css            # Global shared styles
    ├── pages/
    │   ├── HomePage.tsx     # Home: optimizer cards + changes
    │   ├── OptimizerPage.tsx # Benchmark charts (wraps App)
    │   └── ExplorePage.tsx  # Search space explorer (wraps ExploreApp)
    ├── components/          # RootLayout, App, ExploreApp, BenchmarkChart, etc.
    ├── types/               # benchmark.d.ts, exploration.d.ts
    └── utils/               # data parsing, formatting, github API
```

### How data flows
- In production, `data.json` and `explore.json` are plain JSON files on gh-pages
- TanStack Router loaders fetch data before components render
- Route search params (`?cat=X`, `?bench=X`, `?case=X`) replace hash-based navigation
- In dev, `public/{optimizer}/data.json` and `public/{optimizer}/explore.json` provide sample fixture data

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
├── index.html              # SPA entry (same file at all paths)
├── .nojekyll               # Disables Jekyll processing
├── assets/                 # Vite-built JS/CSS bundles
├── edit/
│   ├── index.html          # SPA entry (copy of root index.html)
│   ├── data.json           # Benchmark data (written by bench-data.ts ingest)
│   ├── explore.json        # Exploration data (written by CI deploy step)
│   └── explore/
│       └── index.html      # SPA entry (copy of root index.html)
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
