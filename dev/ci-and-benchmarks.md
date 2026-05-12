# CI/CD and Benchmark Dashboard

This page covers the benchmark pipeline. The separate
`.github/workflows/vimdoc.yml` workflow regenerates
`doc/vimficiency.txt` from `doc-src/**` on pushes to `main`; see
[script-usage.md](./script-usage.md) for that pipeline.

## CI Workflow (`.github/workflows/bench.yml`)

`bench.yml` runs correctness only — `dependency-lint`, `doc-lint`,
`debug-build`, and `test`. Benchmark numbers are deliberately not produced in
CI: GitHub-hosted runners vary by ~10% in wall-clock time, so the numbers
were too noisy to compare across pushes. The bench pipeline moved to a local
hook (next section) on the maintainer's machine, where hardware is stable.

### Dependency setup abstraction

Shared dependency setup is centralized in `.github/actions/setup-ci-deps/action.yml`.

- Always installs `gcc-14` and `g++-14`
- Optionally installs Neovim (version-controlled by workflow env `NEOVIM_VERSION`)
- Caches extracted Neovim under `~/.local/neovim/<version>` with key `neovim-<os>-<version>`
- Adds cached Neovim to `PATH` when enabled

### `test` job (every push and PR)

1. Runs `setup-ci-deps` with Neovim enabled
2. Restores compiler cache via `hendrikmuhs/ccache-action@v1` (`key: test`)
3. Restores CMake dependency cache (`build/_deps`, `deps-v2-*`)
4. Builds in Release mode (`-DVIMF_DEBUG=OFF`, `-DVIMF_TRACK_STATES=OFF`)
5. Runs unit tests: `./build/tests/vimficiency_tests --gtest_brief=1`
6. Runs Lua tests via `tests/lua/run.sh` (FFI smoke)
7. Builds `vimficiency_explore` with tracking on as a smoke test — does not
   run it; the local bench pipeline is what executes it and ingests results

### CI cache and performance notes

- `ccache` cache is restored before build, reducing repeated compile work.
- `build/_deps` cache stores CMake-fetched third-party source/build artifacts (for this repo, primarily GoogleTest and related CMake external content).
- Neovim install is cached by OS + version. On cache hit, CI skips the GitHub release download and untar.
- `apt-get update` and apt package installation are intentionally not cached in this workflow. These commands still run on every fresh GitHub-hosted runner, so `Setup CI deps` can remain one of the slower steps even when all project-level caches hit.

## Local benchmark pipeline (`scripts/bench-local-run.sh`)

Benchmark publishing happens on the maintainer's machine via a `pre-push`
git hook. Setup once after cloning:

```bash
scripts/install-hooks.sh
```

This sets `core.hooksPath=.githooks` so `.githooks/pre-push` fires on every
`git push origin`.

### What the hook does

For each pushed branch ref it applies guardrails (working tree clean, `HEAD`
matches the pushed SHA, on AC power if a `/sys/class/power_supply/AC/online`
sensor exists). Failed guardrails log a skip reason — they never block the
push itself. If guardrails pass, it backgrounds `scripts/bench-local-run.sh
<sha> <branch>` via `setsid nohup`, so the bench keeps running even after
the terminal that ran `git push` is closed.

### What the runner does

Operates in `~/.cache/vimficiency-bench/` so the user's primary checkout is
never touched:

- `repo/` — work clone, fetched and reset to the pushed SHA, used to build
  and run benches (and to check out `HEAD~1` for baseline comparison)
- `gh-pages/` — separate clone of the `gh-pages` branch, written into and
  pushed back to `origin`
- `logs/run-<ts>-<sha>-<branch>.log` — full output of each run
- `lock` — `flock`-based single-flight; concurrent invocations exit
  immediately rather than queue stale runs

Steps:

1. Build current `<sha>` in Release with `VIMF_TRACK_STATES=OFF`
2. Run three suites with `VIMFICIENCY_SEED_MODE=fixed`:
   - `EditOpt.*` → `edit_result.json`
   - `MotionOpt.*` → `motion_result.json`
   - `CompositionOpt.*` → `composition_result.json`
3. Check out `<sha>^`, rebuild, rerun the three suites into
   `baseline_*.json`, check back out to `<sha>`
4. `bun scripts/bench-compare.ts` against the baseline (informational; never
   gates the run)
5. **Main only:** build `vimficiency_explore` with `VIMF_TRACK_STATES=ON`,
   run it for `*_explore.json`, verify non-empty `states`. Also run the test
   suite with `--gtest_output=json` and convert to bench format
6. Build `bench-dashboard/` with `--base=/Vimficiency/` (main) or
   `--base=/Vimficiency/branch/<safe>/` (branch). Main also builds
   `docs-site/`
7. In the `gh-pages` clone: ingest results via `bench-data.ts ingest`,
   strip historical exploration states via `explore-data.ts ingest`, prune
   to 100 entries, copy dashboard/docs assets, `update-branches.ts upsert`
   for branch pushes
8. Commit and push to `origin gh-pages`. On push conflict (race with
   another local run) we fetch + rebase once and retry
9. **Branch only:** find the open PR via `gh pr list --head` and call
   `update-pr-body.ts` to prepend the dashboard link to the PR body

### Re-running manually

If a push happened with the hook disabled or guardrails skipped it, run the
runner directly:

```bash
scripts/bench-local-run.sh <sha> <branch>
```

The script handles main and branch pushes identically to the hook path.

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
