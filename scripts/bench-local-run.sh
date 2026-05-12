#!/usr/bin/env bash
# Local benchmark runner. Mirrors what the deleted benchmark-run /
# benchmark-store CI jobs used to do, but on this machine for consistent
# hardware. Builds the pushed commit in a private cache, runs the three
# suites with fixed seeds, ingests results into a local gh-pages clone, and
# pushes. Main → root dashboard; non-main → branch/<safe-name>/ + PR comment.
#
# Usage:
#   scripts/bench-local-run.sh <pushed-sha> <branch-name>
#
# Invoked in the background by .githooks/pre-push. Logs to
# ~/.cache/vimficiency-bench/logs/. Single-flight via flock — concurrent
# invocations exit immediately rather than queue.
set -euo pipefail

if [ $# -ne 2 ]; then
  echo "usage: $0 <pushed-sha> <branch>" >&2
  exit 2
fi
PUSHED_SHA="$1"
BRANCH="$2"

REPO_ROOT="$(git rev-parse --show-toplevel)"
REPO_URL_RAW="$(git -C "$REPO_ROOT" remote get-url origin)"
REPO_URL="$(printf '%s' "$REPO_URL_RAW" | sed -e 's|^git@github.com:|https://github.com/|' -e 's|\.git$||')"
REPO_FULL_NAME="${REPO_URL#https://github.com/}"
REPO_OWNER="${REPO_FULL_NAME%/*}"
REPO_NAME="${REPO_FULL_NAME#*/}"
SAFE_BRANCH=$(printf '%s' "$BRANCH" | sed 's|/|--|g; s|[^a-zA-Z0-9._-]||g')

if [ "$BRANCH" != "main" ] && { [ -z "$SAFE_BRANCH" ] || [ "$SAFE_BRANCH" = "." ] || [ "$SAFE_BRANCH" = ".." ]; }; then
  echo "[bench-local-run] refusing to deploy with empty sanitized branch name" >&2
  exit 1
fi

CACHE_DIR="${VIMFICIENCY_BENCH_CACHE:-$HOME/.cache/vimficiency-bench}"
WORK_REPO="$CACHE_DIR/repo"
GHPAGES_REPO="$CACHE_DIR/gh-pages"
LOG_DIR="$CACHE_DIR/logs"
LOCK_FILE="$CACHE_DIR/lock"
mkdir -p "$CACHE_DIR" "$LOG_DIR"

# Single-flight. Queuing would let stale commits run after the user has
# already pushed a newer one — the dashboard only cares about the latest.
exec 9>"$LOCK_FILE"
if ! flock -n 9; then
  echo "[bench-local-run] another run in progress; skipping $PUSHED_SHA" >&2
  exit 0
fi

LOG_FILE="$LOG_DIR/run-$(date +%Y%m%d-%H%M%S)-${PUSHED_SHA:0:8}-${SAFE_BRANCH}.log"
exec >"$LOG_FILE" 2>&1
trap 'echo "[bench-local-run] FAILED (exit $?) at $(date)"' ERR

echo "[bench-local-run] starting $BRANCH @ $PUSHED_SHA at $(date)"
echo "  log: $LOG_FILE"

CCACHE_FLAGS=()
if command -v ccache >/dev/null 2>&1; then
  CCACHE_FLAGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

# --- Work clone ---
# Separate clone so the user's primary checkout stays untouched while we
# bounce between $PUSHED_SHA and its parent for the baseline comparison.
if [ ! -d "$WORK_REPO/.git" ]; then
  echo "[setup] creating work clone at $WORK_REPO"
  git clone --no-hardlinks "$REPO_ROOT" "$WORK_REPO"
  git -C "$WORK_REPO" remote set-url origin "$REPO_URL_RAW"
fi
git -C "$WORK_REPO" fetch --quiet origin
git -C "$WORK_REPO" -c advice.detachedHead=false checkout --force "$PUSHED_SHA"

cd "$WORK_REPO"

# --- Build (Release) ---
echo "[build] release at $PUSHED_SHA"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF \
  -DVIMF_TRACK_STATES=OFF \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  "${CCACHE_FLAGS[@]}"
cmake --build build

# --- Run benches ---
echo "[bench] running suites"
run_suite() {
  local filter="$1" out="$2"
  VIMFICIENCY_SEED_MODE=fixed \
    ./build/tests/vimficiency_benchmarks \
      --benchmark_filter="${filter}.*" \
      --benchmark_format=json \
      --benchmark_out="$out"
}
run_suite EditOpt edit_result.json
run_suite MotionOpt motion_result.json
run_suite CompositionOpt composition_result.json

# --- Baseline (HEAD~1) ---
HAS_BASELINE=true
if ! git rev-parse --verify --quiet "${PUSHED_SHA}^" >/dev/null; then
  echo "[baseline] no parent commit; skipping comparison"
  HAS_BASELINE=false
fi
if $HAS_BASELINE; then
  echo "[baseline] building parent of $PUSHED_SHA"
  git -c advice.detachedHead=false checkout --force "${PUSHED_SHA}^"
  cmake --build build
  run_suite EditOpt baseline_edit.json
  run_suite MotionOpt baseline_motion.json
  run_suite CompositionOpt baseline_composition.json
  git -c advice.detachedHead=false checkout --force "$PUSHED_SHA"
  cmake --build build

  echo "[compare] vs baseline"
  bun scripts/bench-compare.ts \
    edit_result.json baseline_edit.json \
    motion_result.json baseline_motion.json \
    composition_result.json baseline_composition.json || true
fi

# --- Main-only artifacts ---
IS_MAIN=false
if [ "$BRANCH" = "main" ]; then
  IS_MAIN=true
fi

if $IS_MAIN; then
  echo "[explore] building tracking-enabled target"
  cmake -B build_track -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF \
    -DVIMF_TRACK_STATES=ON \
    -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
    "${CCACHE_FLAGS[@]}"
  cmake --build build_track --target vimficiency_explore
  VIMFICIENCY_SEED_MODE=fixed ./build_track/tests/vimficiency_explore

  for f in edit_explore.json motion_explore.json composition_explore.json; do
    total=$(jq '[.cases[].states | length] | add // 0' "$f")
    if [ "$total" -eq 0 ]; then
      echo "ERROR: $f has zero explored states across all cases."
      echo "  vimficiency_explore ran but recorded nothing — tracking"
      echo "  instrumentation is likely disconnected. Check VIMF_TRACK_STATES"
      echo "  and the maybeRecordExploredState call sites."
      exit 1
    fi
    echo "  $f: $total explored states recorded"
  done

  echo "[tests] running for timing"
  ./build/tests/vimficiency_tests --gtest_brief=1 --gtest_output=json:test_timing.json
  bun scripts/convert-gtest-timing.ts test_timing.json test_timing_bench.json
fi

# --- Dashboard build ---
BASE_PATH="/$REPO_NAME/"
if ! $IS_MAIN; then
  BASE_PATH="/$REPO_NAME/branch/$SAFE_BRANCH/"
fi
echo "[dashboard] building with base=$BASE_PATH"
( cd bench-dashboard && bun install --frozen-lockfile && bun run build -- --base="$BASE_PATH" )
test -f bench-dashboard/dist/index.html
find bench-dashboard/dist/assets -type f -print -quit | grep -q . || {
  echo "Dashboard build produced no assets"
  exit 1
}

if $IS_MAIN; then
  echo "[docs] building docs site"
  ( cd docs-site && bun install --frozen-lockfile && bun run build )
  test -f docs-site/dist/index.html
  test -f docs-site/dist/01-installation/index.html
fi

# --- Stage artifacts before switching to gh-pages ---
# Helper TS scripts are copied from the source checkout so deploy behavior
# tracks the commit being built rather than whatever versions are on gh-pages.
STAGE_DIR="$(mktemp -d -t vimficiency-bench-stage.XXXXXX)"
cleanup() { rm -rf "$STAGE_DIR"; }
trap cleanup EXIT

cp -r bench-dashboard/dist "$STAGE_DIR/dashboard-dist"
cp scripts/bench-data.ts "$STAGE_DIR/bench-data.ts"
cp scripts/explore-data.ts "$STAGE_DIR/explore-data.ts"
cp scripts/update-branches.ts "$STAGE_DIR/update-branches.ts"
cp scripts/update-pr-body.ts "$STAGE_DIR/update-pr-body.ts"
cp edit_result.json motion_result.json composition_result.json "$STAGE_DIR/"
if $IS_MAIN; then
  cp -r docs-site/dist "$STAGE_DIR/docs-dist"
  cp edit_explore.json motion_explore.json composition_explore.json "$STAGE_DIR/"
  cp test_timing_bench.json "$STAGE_DIR/"
fi

COMMIT_MSG=$(git log -1 --pretty=%s "$PUSHED_SHA")
COMMIT_AUTHOR=$(git log -1 --pretty=%an "$PUSHED_SHA")
COMMIT_TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# --- gh-pages clone ---
if [ ! -d "$GHPAGES_REPO/.git" ]; then
  echo "[setup] creating gh-pages clone at $GHPAGES_REPO"
  git clone --no-hardlinks --branch gh-pages "$REPO_URL_RAW" "$GHPAGES_REPO"
fi
git -C "$GHPAGES_REPO" fetch --quiet origin gh-pages
git -C "$GHPAGES_REPO" checkout --force gh-pages
git -C "$GHPAGES_REPO" reset --hard origin/gh-pages

cd "$GHPAGES_REPO"

if $IS_MAIN; then
  echo "[deploy] main"

  for o in edit motion composition; do
    if [ -d "bench/$o" ] && [ ! -d "$o" ]; then
      echo "  migrating bench/$o -> $o"
      mv "bench/$o" "$o"
    fi
  done
  rm -rf bench

  for pair in \
    "edit:$STAGE_DIR/edit_result.json" \
    "motion:$STAGE_DIR/motion_result.json" \
    "composition:$STAGE_DIR/composition_result.json" \
    "tests:$STAGE_DIR/test_timing_bench.json"; do
    o="${pair%%:*}"; r="${pair#*:}"
    mkdir -p "$o"
    bun "$STAGE_DIR/bench-data.ts" ingest "$o" "$r" \
      --commit-id="$PUSHED_SHA" \
      --commit-msg="$COMMIT_MSG" \
      --commit-ts="$COMMIT_TS" \
      --author="$COMMIT_AUTHOR" \
      --repo-url="$REPO_URL"
  done

  for o in edit motion composition tests; do
    if [ -f "$o/data.json" ]; then
      bun "$STAGE_DIR/bench-data.ts" clean-suites "$o"
      bun "$STAGE_DIR/bench-data.ts" prune "$o"
    fi
  done

  for pair in \
    "edit:$STAGE_DIR/edit_explore.json" \
    "motion:$STAGE_DIR/motion_explore.json" \
    "composition:$STAGE_DIR/composition_explore.json"; do
    o="${pair%%:*}"; r="${pair#*:}"
    mkdir -p "$o"
    bun "$STAGE_DIR/explore-data.ts" ingest "$o" "$r" \
      --commit-id="$PUSHED_SHA" \
      --commit-msg="$COMMIT_MSG" \
      --commit-ts="$COMMIT_TS" \
      --author="$COMMIT_AUTHOR" \
      --repo-url="$REPO_URL"
  done

  for o in edit motion composition; do
    if [ -f "$o/explore.json" ]; then
      bun "$STAGE_DIR/explore-data.ts" prune "$o"
    fi
  done

  cp "$STAGE_DIR/dashboard-dist/index.html" ./index.html
  cp "$STAGE_DIR/dashboard-dist/index.html" ./404.html
  touch .nojekyll

  mkdir -p assets
  rm -rf assets
  mkdir -p assets
  cp -r "$STAGE_DIR/dashboard-dist/assets/." assets/
  find assets -type f -print -quit | grep -q . || {
    echo "Failed to copy dashboard assets to gh-pages root"
    exit 1
  }

  rm -rf docs
  cp -r "$STAGE_DIR/docs-dist" docs
  test -f docs/index.html
  test -f docs/01-installation/index.html

  git add -f index.html 404.html .nojekyll assets/ edit/ motion/ composition/ tests/ docs/
  COMMIT_TITLE="Update benchmark dashboard"
else
  echo "[deploy] branch $BRANCH"
  DEPLOY_DIR="branch/$SAFE_BRANCH"

  for o in edit motion composition; do
    mkdir -p "$DEPLOY_DIR/$o"
    if [ ! -f "$DEPLOY_DIR/$o/data.json" ] && [ -f "$o/data.json" ]; then
      cp "$o/data.json" "$DEPLOY_DIR/$o/data.json"
    fi
  done

  for pair in \
    "edit:$STAGE_DIR/edit_result.json" \
    "motion:$STAGE_DIR/motion_result.json" \
    "composition:$STAGE_DIR/composition_result.json"; do
    o="${pair%%:*}"; r="${pair#*:}"
    bun "$STAGE_DIR/bench-data.ts" ingest "$DEPLOY_DIR/$o" "$r" \
      --commit-id="$PUSHED_SHA" \
      --commit-msg="$COMMIT_MSG" \
      --commit-ts="$COMMIT_TS" \
      --author="$COMMIT_AUTHOR" \
      --repo-url="$REPO_URL"
  done

  cp "$STAGE_DIR/dashboard-dist/index.html" "$DEPLOY_DIR/index.html"
  rm -rf "$DEPLOY_DIR/assets"
  mkdir -p "$DEPLOY_DIR/assets"
  cp -r "$STAGE_DIR/dashboard-dist/assets/." "$DEPLOY_DIR/assets/"
  find "$DEPLOY_DIR/assets" -type f -print -quit | grep -q . || {
    echo "Failed to copy dashboard assets to $DEPLOY_DIR"
    exit 1
  }

  bun "$STAGE_DIR/update-branches.ts" upsert \
    "$BRANCH" "$SAFE_BRANCH" "$REPO_OWNER" "$REPO_FULL_NAME" "$COMMIT_TS"
  test -f branches.json

  git add -f "$DEPLOY_DIR/" branches.json
  COMMIT_TITLE="Update branch dashboard: $BRANCH"
fi

if git diff --cached --quiet; then
  echo "[deploy] no changes to publish"
else
  git commit -m "$COMMIT_TITLE" -m "Source commit: $PUSHED_SHA"
  echo "[push] origin gh-pages"
  if ! git push origin gh-pages; then
    # Single retry after a remote update — covers the case where another local
    # run finished between our fetch and push. The commit content is small;
    # rebase resolves cleanly except for direct conflicts on the same files.
    echo "[push] failed; fetching and retrying once"
    git fetch origin gh-pages
    git rebase origin/gh-pages
    git push origin gh-pages
  fi
fi

# --- PR comment (branch only) ---
if ! $IS_MAIN && command -v gh >/dev/null 2>&1; then
  DASHBOARD_URL="https://${REPO_OWNER}.github.io${BASE_PATH}"
  PR_NUMBER=$(gh pr list --repo "$REPO_FULL_NAME" --head "$BRANCH" --state open --json number --jq '.[0].number // empty' || true)
  if [ -n "${PR_NUMBER:-}" ]; then
    echo "[pr] updating PR #$PR_NUMBER body with dashboard link"
    bun "$WORK_REPO/scripts/update-pr-body.ts" "$PR_NUMBER" "$DASHBOARD_URL" "$PUSHED_SHA" "$REPO_FULL_NAME" \
      || echo "[pr] body update failed (non-fatal)"
  else
    echo "[pr] no open PR for $BRANCH; skipping"
  fi
fi

echo "[bench-local-run] done at $(date)"
