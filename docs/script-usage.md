# Script Usage

All scripts live in `scripts/`. Bun scripts (`.ts`) require [Bun](https://bun.sh); shell scripts require bash; `dep-graph.lua` requires Lua + LuaFileSystem.

---

## CI Scripts

These are called by GitHub Actions workflows and are not normally run by hand.

### `bench-data.ts`
Manages `data.json` benchmark history files on the `gh-pages` branch. Used by the `benchmark-store` job in `bench.yml` for both main and branch deployments.

```
bun scripts/bench-data.ts ingest <dir> <result.json> \
  --commit-id=<sha> --commit-msg=<msg> --commit-ts=<iso> \
  --author=<username> --repo-url=<url>

bun scripts/bench-data.ts prune <dir> [max=100]
bun scripts/bench-data.ts remove <dir> <sha-prefix>
bun scripts/bench-data.ts clean-suites <dir>
```

- **ingest**: appends a benchmark run (from Google Benchmark JSON) to `<dir>/data.json`, migrating from the legacy `data.js` format on first run.
- **prune**: trims the history to the most recent N entries per suite.
- **remove**: deletes all entries matching a commit SHA prefix (used by `remove-bench-data.yml`).
- **clean-suites**: drops stale suite keys that no longer appear in the latest run (handles benchmark renames).

### `explore-data.ts`
Manages `explore.json` A\* exploration state files on `gh-pages`. Used by the `benchmark-store` job on main-branch pushes only.

```
bun scripts/explore-data.ts ingest <dir> <explore.json> \
  --commit-id=<sha> --commit-msg=<msg> --commit-ts=<iso> \
  --author=<username> --repo-url=<url>

bun scripts/explore-data.ts prune <dir> [max=100]
```

Strips the full `states` array from all but the latest entry to keep file sizes manageable, while preserving summary fields (`nodesExplored`, `results`, `context`, `diffs`).

### `bench-compare.ts`
Compares current benchmark results against a baseline and reports regressions. Used by the `benchmark-store` job (all pushes). Writes a markdown table to `$GITHUB_STEP_SUMMARY`.

```
bun scripts/bench-compare.ts [--threshold 1.15] \
  <current.json> <baseline.json> \
  [<current2.json> <baseline2.json> ...]
```

Exits 1 if any benchmark exceeds the threshold ratio; exits 0 otherwise.

### `convert-gtest-timing.ts`
Converts GTest `--gtest_output=json` timing output into Google Benchmark JSON format so it can be ingested by `bench-data.ts`. Used by the `benchmark-store` job on main-branch pushes only.

```
bun scripts/convert-gtest-timing.ts <gtest.json> <output.json>
```

Produces entries for `Tests/Total/All` and `Tests/Suites/<SuiteName>`.

### `update-branches.ts`
Adds or updates a branch entry in `branches.json` at the root of `gh-pages`. Run from within the `gh-pages` checkout during the `benchmark-store` job on non-main pushes. The dashboard home page reads this file to render the "Branch Dashboards" section.

```
bun scripts/update-branches.ts \
  <branch-name> <safe-branch-name> <repo-owner> <iso-timestamp>
```

### `update-pr-body.ts`
Prepends (or updates in-place) a benchmark dashboard link at the top of a PR's description. Run during the `Comment on PR` step in `bench.yml` when a non-main push has an open PR.

```
bun scripts/update-pr-body.ts <pr-number> <dashboard-url> <commit-sha> <repo>
```

Uses HTML comment markers (`<!-- bench-dashboard-link-start/end -->`) so subsequent runs update rather than duplicate the block.

### `lint-module-deps.sh`
Enforces the allowed set of cross-module `#include` edges in `src/`. Run by the `dependency-lint` job in `bench.yml` on every push.

```
./scripts/lint-module-deps.sh
```

Exits 1 and lists violations if any disallowed edge is found (e.g. `Keyboard->Optimizer`).

---

## Local Dev Scripts

These are run manually during development and are not wired into CI.

### `check-test-fixture-names.sh`
Detects ODR violations caused by test fixture classes sharing a name with a production `struct`/`class`. Silent memory corruption can result when the linker merges the two definitions.

```
./scripts/check-test-fixture-names.sh
```

Exits 1 and reports each collision with the file locations of both definitions.

### `test-release.sh`
Builds with `VIMF_DEBUG=OFF` and runs the full test suite. Use this to verify that debug-only code paths are not masking failures.

```
./scripts/test-release.sh
```

### `test-legacy-vim.sh`
Builds with `VIMF_LEGACY_VIM=ON` and runs the full test suite. Use this to verify compatibility with legacy Vim behaviour.

```
./scripts/test-legacy-vim.sh
```

### `dep-graph.lua`
Emits a Graphviz `.dot` file of directory-level `#include` dependencies across `src/`. Useful for visualising the module structure.

**Prerequisites:**
```
sudo pacman -S graphviz
luarocks install luafilesystem
```

**Usage:**
```
lua scripts/dep-graph.lua > deps.dot
dot -Tsvg deps.dot -o deps.svg
```
