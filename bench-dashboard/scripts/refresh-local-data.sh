#!/usr/bin/env bash
set -euo pipefail

DASH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$DASH_DIR/.." && pwd)"
WORK_DIR="$DASH_DIR/.cache/local-data"

mkdir -p "$WORK_DIR"

# Start from clean deterministic fixtures, then layer fresh local results.
bash "$DASH_DIR/scripts/sync-fixtures.sh"

BENCH_BIN="$REPO_DIR/build/tests/vimficiency_benchmarks"
TEST_BIN="$REPO_DIR/build/tests/vimficiency_tests"
if [[ ! -x "$BENCH_BIN" || ! -x "$TEST_BIN" ]]; then
  echo "Missing test binaries. Build first: cmake --build build -j"
  exit 1
fi

echo "Running local benchmarks and tests..."
VIMFICIENCY_SEED_MODE=fixed "$BENCH_BIN" \
  --benchmark_filter="EditOpt.*" \
  --benchmark_out_format=json \
  --benchmark_out="$WORK_DIR/edit_result.json"

VIMFICIENCY_SEED_MODE=fixed "$BENCH_BIN" \
  --benchmark_filter="MotionOpt.*" \
  --benchmark_out_format=json \
  --benchmark_out="$WORK_DIR/motion_result.json"

VIMFICIENCY_SEED_MODE=fixed "$BENCH_BIN" \
  --benchmark_filter="CompositionOpt.*" \
  --benchmark_out_format=json \
  --benchmark_out="$WORK_DIR/composition_result.json"

VIMFICIENCY_SEED_MODE=fixed "$TEST_BIN" --gtest_brief=1 \
  --gtest_output=json:"$WORK_DIR/test_timing.json"

bun "$REPO_DIR/scripts/convert-gtest-timing.ts" \
  "$WORK_DIR/test_timing.json" \
  "$WORK_DIR/test_timing_bench.json"

COMMIT_ID="$(git -C "$REPO_DIR" rev-parse HEAD 2>/dev/null || echo local)"
COMMIT_MSG="$(git -C "$REPO_DIR" log -1 --pretty=%s 2>/dev/null || echo 'local data refresh')"
COMMIT_TS="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
AUTHOR="$(git -C "$REPO_DIR" config user.name 2>/dev/null || echo local)"
REPO_URL="$(git -C "$REPO_DIR" config --get remote.origin.url 2>/dev/null || echo 'https://github.com/rickyah/Vimficiency')"

if [[ "$REPO_URL" =~ ^git@github.com:(.+)\.git$ ]]; then
  REPO_URL="https://github.com/${BASH_REMATCH[1]}"
elif [[ "$REPO_URL" =~ ^https://github.com/.+\.git$ ]]; then
  REPO_URL="${REPO_URL%.git}"
fi

bun "$REPO_DIR/scripts/bench-data.ts" ingest "$DASH_DIR/public/edit" "$WORK_DIR/edit_result.json" \
  --commit-id="$COMMIT_ID" \
  --commit-msg="$COMMIT_MSG" \
  --commit-ts="$COMMIT_TS" \
  --author="$AUTHOR" \
  --repo-url="$REPO_URL"

bun "$REPO_DIR/scripts/bench-data.ts" ingest "$DASH_DIR/public/motion" "$WORK_DIR/motion_result.json" \
  --commit-id="$COMMIT_ID" \
  --commit-msg="$COMMIT_MSG" \
  --commit-ts="$COMMIT_TS" \
  --author="$AUTHOR" \
  --repo-url="$REPO_URL"

bun "$REPO_DIR/scripts/bench-data.ts" ingest "$DASH_DIR/public/composition" "$WORK_DIR/composition_result.json" \
  --commit-id="$COMMIT_ID" \
  --commit-msg="$COMMIT_MSG" \
  --commit-ts="$COMMIT_TS" \
  --author="$AUTHOR" \
  --repo-url="$REPO_URL"

bun "$REPO_DIR/scripts/bench-data.ts" ingest "$DASH_DIR/public/tests" "$WORK_DIR/test_timing_bench.json" \
  --commit-id="$COMMIT_ID" \
  --commit-msg="$COMMIT_MSG" \
  --commit-ts="$COMMIT_TS" \
  --author="$AUTHOR" \
  --repo-url="$REPO_URL"

echo "Refreshed local dashboard data in public/."
