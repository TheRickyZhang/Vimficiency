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

# ccache is required, not optional. Without it every run rebuilds from
# scratch (current commit, plus HEAD~1 baseline) — pushing turns into a
# 10-20 minute wait. ccache hits make the steady state ~3 min.
if ! command -v ccache >/dev/null 2>&1; then
  echo "[bench-local-run] ccache not found. Install it before running:" >&2
  echo "  sudo pacman -S ccache    # Arch" >&2
  echo "  sudo apt install ccache  # Debian/Ubuntu" >&2
  echo "  brew install ccache      # macOS" >&2
  exit 1
fi

REPO_ROOT="$(git rev-parse --show-toplevel)"
REPO_URL_RAW="$(git -C "$REPO_ROOT" remote get-url origin)"
REPO_URL="$(printf '%s' "$REPO_URL_RAW" | sed -e 's|^git@github.com:|https://github.com/|' -e 's|\.git$||')"
REPO_NAME="${REPO_URL##*/}"
SAFE_BRANCH=$(printf '%s' "$BRANCH" | sed 's|/|--|g; s|[^a-zA-Z0-9._-]||g')

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

# Failure surfacing. The runner is backgrounded with stdout/stderr already
# redirected to /dev/null by the hook, so without these mechanisms a crash
# is invisible to the user:
#   - $STATUS_FILE: pre-push hook reads this on the next push and prints a
#     warning if the last exit was non-zero. Successful runs overwrite it,
#     so the warning naturally clears.
#   - notify-send: desktop notification on completion (low urgency on
#     success, critical on failure). Silently skipped if not installed.
STATUS_FILE="$CACHE_DIR/last-status"
STAGE_DIR=""

cleanup_and_report() {
  local rc=$?
  [ -n "$STAGE_DIR" ] && rm -rf "$STAGE_DIR"

  printf 'exit_code=%s\nsha=%s\nbranch=%s\nlog=%s\nended_at=%s\n' \
    "$rc" "$PUSHED_SHA" "$BRANCH" "$LOG_FILE" \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    > "$STATUS_FILE"

  if command -v notify-send >/dev/null 2>&1; then
    if [ "$rc" -eq 0 ]; then
      notify-send -u low -a vimficiency \
        "Bench OK" "$BRANCH @ ${PUSHED_SHA:0:8}" 2>/dev/null || true
    else
      notify-send -u critical -a vimficiency \
        "Bench FAILED (exit $rc)" "$BRANCH @ ${PUSHED_SHA:0:8}
Log: $LOG_FILE" 2>/dev/null || true
    fi
  fi

  [ "$rc" -ne 0 ] && echo "[bench-local-run] FAILED (exit $rc) at $(date)"
}
trap cleanup_and_report EXIT

echo "[bench-local-run] starting $BRANCH @ $PUSHED_SHA at $(date)"
echo "  log: $LOG_FILE"

CCACHE_FLAGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)

# Clean up any worktree entries pointing at directories we previously
# deleted by hand; harmless when there are none.
git -C "$REPO_ROOT" worktree prune

# --- Source worktree ---
# Detached worktree at PUSHED_SHA, sharing .git with the user's main repo.
# Two reasons over a separate clone: (1) the commit being pushed is already
# in the main repo's object store, so we sidestep the pre-push race where
# `git fetch origin` runs before GitHub has the new tree; (2) one shared
# .git instead of duplicating it. Pinned detached HEAD means whatever the
# user does in their main checkout next can't move our source out from
# under the in-progress build.

# Migrate prior clone-based layout if present.
if [ -d "$WORK_REPO/.git" ]; then
  echo "[migrate] replacing prior work-clone with worktree at $WORK_REPO"
  rm -rf "$WORK_REPO"
fi

if [ ! -e "$WORK_REPO/.git" ]; then
  git -C "$REPO_ROOT" worktree add --detach --force "$WORK_REPO" "$PUSHED_SHA"
else
  git -C "$WORK_REPO" -c advice.detachedHead=false checkout --force "$PUSHED_SHA"
fi

cd "$WORK_REPO"

# --- Build (Release) ---
echo "[build] release at $PUSHED_SHA"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF \
  -DVIMF_TRACK_STATES=OFF \
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

# --- Main/branch split (needed for baseline lookup below) ---
IS_MAIN=false
if [ "$BRANCH" = "main" ]; then
  IS_MAIN=true
fi

# --- gh-pages worktree (eager so the baseline step can read from it) ---
# Same .git sharing argument as the source worktree above. The gh-pages
# worktree is on the actual gh-pages branch (not detached) so commits we
# make below advance the branch and can be pushed straight to origin.

# Migrate prior clone-based layout if present.
if [ -d "$GHPAGES_REPO/.git" ]; then
  echo "[migrate] replacing prior gh-pages-clone with worktree at $GHPAGES_REPO"
  rm -rf "$GHPAGES_REPO"
fi

git -C "$REPO_ROOT" fetch --quiet origin gh-pages
if [ ! -e "$GHPAGES_REPO/.git" ]; then
  # -B (re)points the local gh-pages branch at origin/gh-pages; --force
  # tolerates stale worktree entries that worktree prune already cleared.
  git -C "$REPO_ROOT" worktree add -B gh-pages --force "$GHPAGES_REPO" origin/gh-pages
fi
git -C "$GHPAGES_REPO" reset --hard origin/gh-pages

# --- Baseline: pulled from the parent commit's previously-recorded numbers ---
# Same physical machine that recorded the parent is recording this commit,
# so the stored numbers are directly comparable. Rebuilding HEAD~1 the way
# CI did was only necessary because CI runners are non-uniform across runs.
# If the parent was never benched (first run on this machine, or the parent
# was rebased away), we skip the comparison — it's informational.
COMPARE_ARGS=()
if PARENT_SHA=$(git rev-parse --verify "${PUSHED_SHA}^" 2>/dev/null); then
  for triple in "edit:edit_result.json:baseline_edit.json" \
                "motion:motion_result.json:baseline_motion.json" \
                "composition:composition_result.json:baseline_composition.json"; do
    o="${triple%%:*}"; rest="${triple#*:}"; curr="${rest%%:*}"; base="${rest#*:}"
    if bun scripts/bench-baseline-from-stored.ts "$GHPAGES_REPO/$o" "$PARENT_SHA" "$base"; then
      COMPARE_ARGS+=("$curr" "$base")
    fi
  done
fi

if [ ${#COMPARE_ARGS[@]} -gt 0 ]; then
  echo "[compare] vs stored parent ${PARENT_SHA:0:8}"
  bun scripts/bench-compare.ts "${COMPARE_ARGS[@]}" || true
else
  echo "[compare] no stored baseline for parent; skipping"
fi

if $IS_MAIN; then
  echo "[explore] building tracking-enabled target"
  cmake -B build_track -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF \
    -DVIMF_TRACK_STATES=ON \
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
# cleanup_and_report (registered earlier) removes $STAGE_DIR on EXIT.

cp -r bench-dashboard/dist "$STAGE_DIR/dashboard-dist"
cp scripts/bench-data.ts "$STAGE_DIR/bench-data.ts"
cp scripts/explore-data.ts "$STAGE_DIR/explore-data.ts"
cp edit_result.json motion_result.json composition_result.json "$STAGE_DIR/"
if $IS_MAIN; then
  cp -r docs-site/dist "$STAGE_DIR/docs-dist"
  cp edit_explore.json motion_explore.json composition_explore.json "$STAGE_DIR/"
  cp test_timing_bench.json "$STAGE_DIR/"
fi

COMMIT_MSG=$(git log -1 --pretty=%s "$PUSHED_SHA")
COMMIT_AUTHOR=$(git log -1 --pretty=%an "$PUSHED_SHA")
COMMIT_TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

# gh-pages was cloned + reset earlier (so the baseline lookup could read it).
# Re-fetch + reset now in case the tree drifted while we were building, so
# the deploy lands on top of the latest published state.
git -C "$GHPAGES_REPO" fetch --quiet origin gh-pages
git -C "$GHPAGES_REPO" reset --hard origin/gh-pages

cd "$GHPAGES_REPO"

echo "[deploy]"

# Migration from legacy `bench/<optimizer>/` layout to flat `<optimizer>/`.
# Idempotent; runs every time but only acts the first time it sees the
# legacy layout.
for o in edit motion composition; do
  if [ -d "bench/$o" ] && [ ! -d "$o" ]; then
    echo "  migrating bench/$o -> $o"
    mv "bench/$o" "$o"
  fi
done
rm -rf bench

# Ingest the three benchmark suites — every push contributes a data point.
# Whether $BRANCH is "main" or a feature branch, the data lands on the same
# root timeline (single-source-of-truth model). Branch SHAs that never
# reach main will appear on the chart and stay there as historical points;
# that's accepted as the cost of avoiding any cross-branch promotion logic.
for pair in \
  "edit:$STAGE_DIR/edit_result.json" \
  "motion:$STAGE_DIR/motion_result.json" \
  "composition:$STAGE_DIR/composition_result.json"; do
  o="${pair%%:*}"; r="${pair#*:}"
  mkdir -p "$o"
  bun "$STAGE_DIR/bench-data.ts" ingest "$o" "$r" \
    --commit-id="$PUSHED_SHA" \
    --commit-msg="$COMMIT_MSG" \
    --commit-ts="$COMMIT_TS" \
    --author="$COMMIT_AUTHOR" \
    --repo-url="$REPO_URL"
done

# Test-suite timing is only measured on main (the run executes the test
# binary with --gtest_output=json; branches skip that step to stay fast).
if $IS_MAIN; then
  mkdir -p tests
  bun "$STAGE_DIR/bench-data.ts" ingest tests "$STAGE_DIR/test_timing_bench.json" \
    --commit-id="$PUSHED_SHA" \
    --commit-msg="$COMMIT_MSG" \
    --commit-ts="$COMMIT_TS" \
    --author="$COMMIT_AUTHOR" \
    --repo-url="$REPO_URL"
fi

PRUNE_DIRS=(edit motion composition)
$IS_MAIN && PRUNE_DIRS+=(tests)
for o in "${PRUNE_DIRS[@]}"; do
  if [ -f "$o/data.json" ]; then
    bun "$STAGE_DIR/bench-data.ts" clean-suites "$o"
    bun "$STAGE_DIR/bench-data.ts" prune "$o"
  fi
done

# Exploration data — main only, same rationale as test timing.
if $IS_MAIN; then
  for pair in \
    "edit:$STAGE_DIR/edit_explore.json" \
    "motion:$STAGE_DIR/motion_explore.json" \
    "composition:$STAGE_DIR/composition_explore.json"; do
    o="${pair%%:*}"; r="${pair#*:}"
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
fi

cp "$STAGE_DIR/dashboard-dist/index.html" ./index.html
cp "$STAGE_DIR/dashboard-dist/index.html" ./404.html
touch .nojekyll

rm -rf assets
mkdir -p assets
cp -r "$STAGE_DIR/dashboard-dist/assets/." assets/
find assets -type f -print -quit | grep -q . || {
  echo "Failed to copy dashboard assets to gh-pages root"
  exit 1
}

if $IS_MAIN; then
  rm -rf docs
  cp -r "$STAGE_DIR/docs-dist" docs
  test -f docs/index.html
  test -f docs/01-installation/index.html
fi

git add -f index.html 404.html .nojekyll assets/ edit/ motion/ composition/
$IS_MAIN && git add -f tests/ docs/
COMMIT_TITLE="Update benchmark dashboard"

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

echo "[bench-local-run] done at $(date)"
