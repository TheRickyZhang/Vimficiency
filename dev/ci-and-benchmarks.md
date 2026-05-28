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
5. Runs the fast correctness gate via `scripts/vimfy_tests`: unit, approval,
   property and safety exploration with the fixed CI FuzzTest runner seed, and
   Lua/FFI smoke tests
6. Builds `vimfy_explore` with tracking on as a smoke test — does not
   run it; the local bench pipeline is what executes it and ingests results

### CI cache and performance notes

- `ccache` cache is restored before build, reducing repeated compile work.
- `build/_deps` cache stores CMake-fetched third-party source/build artifacts
  (for this repo, GoogleTest, FuzzTest, benchmark, msgpack, json, and their
  related CMake external content).
- Neovim install is cached by OS + version. On cache hit, CI skips the GitHub release download and untar.
- `apt-get update` and apt package installation are intentionally not cached in this workflow. These commands still run on every fresh GitHub-hosted runner, so `Setup CI deps` can remain one of the slower steps even when all project-level caches hit.

## Benchmark pipeline (`scripts/bench-local-run.sh`)

Two triggers drive the same `bench-local-run.sh` script:

1. **`main` pushes** — `.github/workflows/bench-main.yml` runs on a
   self-hosted macOS runner (the "spare machine"). Fires on every push to
   main, including PR merges done via the web UI. Source of truth for the
   chart's main-history points.
2. **Feature branch pushes** — `.githooks/pre-push` on the maintainer's
   dev machine. Local hook, fires on `git push origin <feature-branch>`.
   Useful for pre-merge perf intuition; not on the chart's main-history
   path.

Cross-machine note: feature-branch entries come from the dev machine and
main entries from the spare. The two hardware profiles produce different
absolute numbers. Within a single regime (main-to-main, feature-to-feature
on the same dev machine) comparisons are stable; cross-regime baseline
comparisons (e.g. `bench-compare` against a parent SHA benched on the
other machine) are apples-to-oranges and worth reading with that caveat.

### Pre-push hook (feature branches)

Installs itself the first time you run `cmake -B build` — the top-level
`CMakeLists.txt` sets `core.hooksPath=.githooks` if it isn't already, so
`.githooks/pre-push` fires on every `git push origin`. The install step is
skipped in CI (`CI` env var present) since CI clones don't push from these
checkouts.

For each pushed branch ref the hook applies guardrails (working tree
clean, `HEAD` matches the pushed SHA, on AC power if a
`/sys/class/power_supply/AC/online` sensor exists). It also **skips
`main`** — those go through the self-hosted runner. Failed guardrails log
a skip reason; they never block the push itself. If guardrails pass, it
backgrounds `scripts/bench-local-run.sh <sha> <branch>` via `setsid
nohup`, so the bench keeps running even after the terminal that ran `git
push` is closed.

### Self-hosted macOS runner (`main`)

GitHub workflow `bench-main.yml` on `runs-on: [self-hosted, macOS,
vimficiency-bench]`, triggered `on: push: branches: [main]`. The job
checks out the repo with `fetch-depth: 0` (the baseline-lookup step needs
HEAD~1; gh-pages is fetched explicitly inside the script) and invokes the
same `bench-local-run.sh` with `CI=true` set. The CI env gate inside the
script skips the `flock` single-flight (the workflow's `concurrency:
group: bench-main` already serializes runs).

The runner is registered to the repo via `Settings → Actions → Runners`
and runs as a launchd service via the installer's `./svc.sh install`.
Sleep is disabled via `pmset` so PR merges during off-hours bench
promptly. Deps installed via Homebrew (`ccache cmake neovim jq bun`);
Apple clang from `xcode-select --install` handles the C++ build.

### What the runner does

Operates in `~/.cache/vimficiency-bench/` so the user's primary checkout is
never touched. Two `git worktree`s sharing `.git` with the main repo:

- `repo/` — detached worktree at the pushed SHA, used to build and run benches
- `gh-pages/` — worktree on the `gh-pages` branch, written into and pushed back to `origin`
- `logs/run-<ts>-<sha>-<branch>.log` — full output of each run
- `last-status` — written on every exit; pre-push hook reads this so failures from a previous run surface on the next push
- `lock` — `flock`-based single-flight; concurrent invocations exit immediately rather than queue stale runs

### Single-source data model

Every bench — main on the spare machine, feature branches on the dev
machine — ingests into the same root timeline at
`gh-pages/{edit,motion,composition}/data.json`. There are no per-branch
dashboards, no promotion-on-PR-merge logic, no `branches.json` index.
Each commit in the chart corresponds to an actual measurement on one of
the two machines (with the cross-regime caveat noted above). Feature-branch
SHAs that never reach `main` (squashed away, rebased, abandoned) still
appear on the timeline as historical points — accepted as the cost of
keeping the bench/merge concerns decoupled.

Main-only extras are conditional on `BRANCH=main` (a cheap optimization,
not a correctness boundary): exploration data via `vimfy_explore`
with `VIMF_TRACK_STATES=ON`, C++ test-suite timing via
`--gtest_output=json`, and the docs-site build. Branch pushes skip these to
stay fast.

Steps:

1. Build pushed `<sha>` in Release with `VIMF_TRACK_STATES=OFF`
2. Run three suites with fixed benchmark fixtures:
   - `EditOpt.*` → `edit_result.json`
   - `MotionOpt.*` → `motion_result.json`
   - `CompositionOpt.*` → `composition_result.json`
3. Baseline comparison: look up the parent SHA's stored entry in `gh-pages/{edit,motion,composition}/data.json` via `scripts/bench-baseline-from-stored.ts`, feed it to `bench-compare.ts`. Skipped when the parent has no stored entry (first run on this machine, parent was rebased away, etc.) — informational, never gates the run.
4. **Main only:** build and run `vimfy_explore` for exploration data; time the unit + approval binaries (`convert-gtest-timing.ts`) and snapshot property + safety coverage (`convert-gtest-coverage.ts`)
5. Build `bench-dashboard/` with `--base=/Vimficiency/`. **Main only:** also build `docs-site/`
6. In the `gh-pages` worktree: ingest results, prune to 100 entries, ingest exploration (main only), copy dashboard/docs assets
7. Commit + push to `origin gh-pages`. On push conflict (race with another local run) fetch + rebase once and retry

### Failure surfacing

- **Self-hosted runner (main):** the workflow run shows up under the
  Actions tab; failures send the standard GitHub notification. Logs are
  retained per GitHub's defaults.
- **Local hook (feature branches):** runner is backgrounded with output
  redirected, so crashes aren't immediately visible. Mechanisms:
  - `notify-send` desktop notification on completion — critical urgency on
    failure, low on success. Silently skipped if no notification daemon.
  - `~/.cache/vimficiency-bench/last-status` records exit code, SHA,
    branch, and log path.
  - The next `git push` (via the pre-push hook) reads `last-status` and
    prints a warning if the previous exit was non-zero. A successful run
    overwrites the status so the warning clears.

### Re-running manually

If a push happened with the hook disabled or guardrails skipped it, run
the runner directly from any machine that has the build deps installed:

```bash
scripts/bench-local-run.sh <sha> <branch>
```

The script's behavior is identical whether `<branch>` is `main` or a
feature branch — only the optional main-only extras differ.

## Self-hosted macOS runner setup (one-time)

Steps to register the spare MacBook as the runner backing
`bench-main.yml`. Run as your normal user; nothing here needs `sudo`
except where noted.

1. **Toolchain bootstrap.** Apple clang for the C++ build, Homebrew so
   the workflow has something to install bench deps into. Everything else
   (`ccache`, `cmake`, `neovim`, `jq`, `bun`) is installed idempotently by
   `bench-main.yml`'s "Install bench deps" step on every run, so it
   doesn't need to live in this checklist.

   ```bash
   xcode-select --install
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **Disable sleep so PR merges during off-hours bench promptly.**

   ```bash
   sudo pmset -a sleep 0
   sudo pmset -a disksleep 0
   sudo pmset -c disablesleep 1   # AC-powered: never sleep, even with lid closed
   ```

3. **Register the runner.** In the repo on GitHub: Settings → Actions →
   Runners → New self-hosted runner → macOS. The page gives a download +
   `./config.sh` command with a registration token (1hr expiry). When it
   asks for labels, enter `vimficiency-bench` so the workflow's
   `runs-on: [self-hosted, macOS, vimficiency-bench]` matches.

4. **Install as a launchd service** so it auto-starts on boot and
   survives logout:

   ```bash
   cd ~/actions-runner       # or wherever you extracted it
   ./svc.sh install
   ./svc.sh start
   ./svc.sh status           # should show running
   ```

5. **Verify.** The runner appears as "Idle" in the GitHub Settings page.
   Trigger a test run by pushing any commit to main, or by re-running the
   most recent `bench-main` workflow from the Actions tab.

### First-run gotchas

- First main push after registration builds everything from scratch
  (cmake fetches GoogleTest/FuzzTest/benchmark/msgpack/etc., compiles
  Release with no ccache history). Expect ~10-20 min. Steady-state with a
  warm ccache is ~3-5 min.
- Apple clang may flag warnings GCC doesn't. If the first build fails,
  fix the warnings or set `-DCMAKE_CXX_FLAGS=-Wno-<flag>` in the workflow
  rather than hacking around it.
- Numbers will land in a different absolute range than the dev-machine
  history — the chart shows a step at the cutover. Not a bug; the spare
  machine is a different CPU.

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

`vimfy_explore` outputs JSON → the local deploy runner merges it into
`{optimizer}/explore.json` on gh-pages. The dashboard fetches this JSON
directly.

### Test timing data (`tests/data.json`) and coverage (`tests/coverage.json`)

On `main`, the local deploy runner records `--gtest_output=json` for all four
test binaries, but splits them by what their wall-time means:

- **Unit + Approval** are deterministic, so `scripts/convert-gtest-timing.ts`
  turns them into Google Benchmark-shaped data under the `Tests` suite —
  `Tests/Binaries/{Unit,Approval}` (per-binary total) and
  `Tests/Cases/{Unit,Approval}/<Suite>.<Case>` (per-case). Ingested into
  `tests/data.json` as a per-commit time series. The home page charts the two
  per-binary totals as one multi-line chart; each suite page shows its
  binary-total trend on top plus per-case charts (no per-suite total).
- **Property + Safety** run FuzzTest in unit mode, whose wall-time is a fixed
  watchdog floor and carries no signal (see `dev/decisions.md`). They are run
  with `FUZZTEST_FUZZ_FOR=0` purely to enumerate cases, and
  `scripts/convert-gtest-coverage.ts` writes a latest-snapshot
  `tests/coverage.json` (suites → cases + pass status + counts). Their dashboard
  pages show coverage, not a duration trend.

The historical `tests/data.json` was migrated to this shape once via
`scripts/migrate-tests-data.ts` (drops `Tests/Total/All`, `Tests/Suites/*`, and
property/safety timing; upgrades legacy unit-only entries). That script is
single-use and may be deleted.

Unrelated to the dashboard but in the same area: the fuzz binaries themselves
are fast now because `tests/patches/fuzztest-watchdog.patch` (applied via the
`PATCH_COMMAND` on the pinned `fuzztest` `FetchContent_Declare`) defuses the 1s
per-test watchdog teardown. `scripts/vimfy_tests` exposes `--for <dur>` to set
each FUZZ_TEST's `FUZZTEST_FUZZ_FOR` budget, which now shortens runs for real.

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

### Verifying the dashboard build locally
```bash
cd bench-dashboard && bun install --frozen-lockfile && bun run build
```
This mirrors the dashboard build step used by the local deploy runner.

## gh-pages Branch Layout

```
gh-pages/
├── index.html              # SPA entry (same file at all paths)
├── .nojekyll               # Disables Jekyll processing
├── assets/                 # Vite-built JS/CSS bundles
├── edit/
│   ├── index.html          # SPA entry (copy of root index.html)
│   ├── data.json           # Benchmark data (written by bench-data.ts ingest)
│   ├── explore.json        # Exploration data (written by the local deploy runner)
│   └── explore/
│       └── index.html      # SPA entry (copy of root index.html)
├── motion/
│   ├── index.html
│   ├── data.json
│   ├── explore.json
│   └── explore/
│       └── index.html
├── composition/
│   ├── index.html
│   ├── data.json
│   ├── explore.json
│   └── explore/
│       └── index.html
├── tests/
│   ├── data.json           # Unit + Approval timing (per-commit time series)
│   └── coverage.json       # Property + Safety coverage (latest snapshot)
└── docs/
```

## Gitignore

`bench-dashboard/node_modules/` and `bench-dashboard/dist/` are gitignored. Source files, `bun.lock`, and `public/` are tracked.
