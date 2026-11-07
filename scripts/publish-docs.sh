#!/usr/bin/env bash
# Replaces docs/ on gh-pages with web/docs/dist.
# Usage: scripts/publish-docs.sh <source-sha>
#
# bench-local-run.sh owns everything else on gh-pages, so a rejected push only
# needs this commit rebuilt on the new tip.
set -euo pipefail

SOURCE_SHA="$1"

cd "$(dirname "$0")/.."
DIST="$PWD/web/docs/dist"
PAGES="$(mktemp -d)"

git fetch --quiet --depth=1 origin gh-pages
git worktree add --quiet --detach "$PAGES" FETCH_HEAD
cd "$PAGES"

for attempt in 1 2 3; do
  rm -rf docs
  cp -r "$DIST" docs
  git add -f docs

  if git diff --cached --quiet; then
    echo "[publish-docs] no changes to publish"
    exit 0
  fi

  git -c user.name="github-actions[bot]" \
    -c user.email="41898282+github-actions[bot]@users.noreply.github.com" \
    commit --quiet -m "Update docs site" -m "Source commit: $SOURCE_SHA"

  if git push --quiet origin HEAD:refs/heads/gh-pages; then
    exit 0
  fi

  echo "[publish-docs] push rejected on attempt $attempt; retrying on the new tip"
  git fetch --quiet --depth=1 origin gh-pages
  git reset --quiet --hard FETCH_HEAD
done

echo "[publish-docs] could not push to gh-pages" >&2
exit 1
