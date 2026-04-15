#!/usr/bin/env bash
# Concatenate user-facing markdown chapters (doc-src/NN-*.md) into a
# single source file (build/vimficiency.md) suitable for panvimdoc
# conversion to doc/vimficiency.txt. Run from any directory.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$repo_root/build"
out="$repo_root/build/vimficiency.md"

cd "$repo_root"

strip_chapter() {
  # - drop navigation lines (contain **[Index]...**)
  # - drop horizontal-rule separators that frame nav
  # - demote every heading by one level so the combined doc has a single
  #   h1 (plugin name) with chapters at h2 and their subsections at h3+.
  #   Ordered deepest-first so a demoted line never re-matches a later rule.
  # - strip links pointing at *.md targets, keep display text
  sed -E \
    -e '/\*\*\[Index\]/d' \
    -e '/^---$/d' \
    -e 's/^#### /##### /' \
    -e 's/^### /#### /' \
    -e 's/^## /### /' \
    -e 's/^# /## /' \
    -e 's/\[([^][]+)\]\(([^)]*\.md[^)]*)\)/\1/g' \
    "$1"
}

{
  printf '# vimficiency\n\n'
  printf 'Vimficiency watches how you edit and surfaces shorter keystroke '
  printf 'sequences that produce the same result. You keep editing Vim the '
  printf 'way you already do — it surfaces better motions in the background '
  printf 'and lets you replay them side-by-side to learn.\n\n'

  for f in doc-src/[0-9][0-9]-*.md; do
    strip_chapter "$f"
    printf '\n'
  done
} > "$out"

echo "Wrote $out"
