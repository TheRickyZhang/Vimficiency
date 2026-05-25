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
5. Runs the fast correctness gate via `scripts/vimfy_tests`: unit, expect,
   seeded property, seeded safety, and Lua/FFI smoke tests
6. Builds `vimfy_explore` with tracking on as a smoke test — does not
   run it; the local bench pipeline is what executes it and ingests results

### CI cache and performance notes

- `ccache` cache is restored before build, reducing repeated compile work.
- `build/_deps` cache stores CMake-fetched third-party source/build artifacts
  (for this repo, GoogleTest, FuzzTest, benchmark, msgpack, json, and their
  related CMake external content).
- Neovim install is cached by OS + version. On cache hit, CI skips the GitHub release download and untar.
- `apt-get update` and apt package installation are intentionally not cached in this workflow. These commands still run on every fresh GitHub-hosted runner, so `Setup CI deps` can remain one of the slower steps even when all project-level caches hit.

## Local benchmark pipeline (`scripts/bench-local-run.sh`)

Benchmark publishing happens on the maintainer's machine via a `pre-push`
git hook. The hook installs itself the first time you run `cmake -B build`
— the top-level `CMakeLists.txt` sets `core.hooksPath=.githooks` if it
isn't already, so `.githooks/pre-push` fires on every `git push origin`.
The install step is skipped in CI (`CI` env var present) since CI clones
don't push from these checkouts.

### What the hook does

For each pushed branch ref it applies guardrails (working tree clean, `HEAD`
matches the pushed SHA, on AC power if a `/sys/class/power_supply/AC/online`
sensor exists). Failed guardrails log a skip reason — they never block the
push itself. If guardrails pass, it backgrounds `scripts/bench-local-run.sh
<sha> <branch>` via `setsid nohup`, so the bench keeps running even after
the terminal that ran `git push` is closed.

### What the runner does

Operates in `~/.cache/vimficiency-bench/` so the user's primary checkout is
never touched. Two `git worktree`s sharing `.git` with the main repo:

- `repo/` — detached worktree at the pushed SHA, used to build and run benches
- `gh-pages/` — worktree on the `gh-pages` branch, written into and pushed back to `origin`
- `logs/run-<ts>-<sha>-<branch>.log` — full output of each run
- `last-status` — written on every exit; pre-push hook reads this so failures from a previous run surface on the next push
- `lock` — `flock`-based single-flight; concurrent invocations exit immediately rather than queue stale runs

### Single-source data model

Every push — `main` or feature branch — ingests its results into the same
root timeline at `gh-pages/{edit,motion,composition}/data.json`. There are
no per-branch dashboards, no promotion-on-PR-merge logic, no
`branches.json` index. Each commit in the chart corresponds to an actual
measurement on the maintainer's hardware. The trade-off: feature-branch
SHAs that never reach `main` (squashed away, rebased, abandoned) still
appear on the timeline as historical points — accepted as the cost of
keeping the bench/merge concerns decoupled.

Main-only extras are conditional on `BRANCH=main` (a cheap optimization,
not a correctness boundary): exploration data via `vimfy_explore`
with `VIMF_TRACK_STATES=ON`, test-suite timing via `--gtest_output=json`,
and the docs-site build. Branch pushes skip these to stay fast.

Steps:

1. Build pushed `<sha>` in Release with `VIMF_TRACK_STATES=OFF`
2. Run three suites with fixed benchmark fixtures:
   - `EditOpt.*` → `edit_result.json`
   - `MotionOpt.*` → `motion_result.json`
   - `CompositionOpt.*` → `composition_result.json`
3. Baseline comparison: look up the parent SHA's stored entry in `gh-pages/{edit,motion,composition}/data.json` via `scripts/bench-baseline-from-stored.ts`, feed it to `bench-compare.ts`. Skipped when the parent has no stored entry (first run on this machine, parent was rebased away, etc.) — informational, never gates the run.
4. **Main only:** build and run `vimfy_explore` for exploration data; run the test suite and convert timing to bench format
5. Build `bench-dashboard/` with `--base=/Vimficiency/`. **Main only:** also build `docs-site/`
6. In the `gh-pages` worktree: ingest results, prune to 100 entries, ingest exploration (main only), copy dashboard/docs assets
7. Commit + push to `origin gh-pages`. On push conflict (race with another local run) fetch + rebase once and retry

### Failure surfacing

The runner is backgrounded with output redirected to a log file, so a crash isn't immediately visible. Three mechanisms close that gap:

- `notify-send` desktop notification on completion — critical urgency on failure, low on success. Silently skipped if no notification daemon
- `~/.cache/vimficiency-bench/last-status` records exit code, SHA, branch, and log path
- The next `git push` (via the pre-push hook) reads `last-status` and prints a warning if the previous exit was non-zero. A successful run overwrites the status so the warning clears

### Re-running manually

If a push happened with the hook disabled or guardrails skipped it, run the
runner directly:

```bash
scripts/bench-local-run.sh <sha> <branch>
```

The script's behavior is identical whether `<branch>` is `main` or a
feature branch — only the optional main-only extras differ.

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
          { "name": "EditOpt/BufferSize/1", "value": 1.693, "unit": "ms/iter" }
        ]
      }
    ]
  }
}
```

### Exploration data (`explore.json`)

`vimfy_explore` outputs JSON → CI merges it into `{optimizer}/explore.json` on gh-pages. The dashboard fetches this JSON directly.

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
