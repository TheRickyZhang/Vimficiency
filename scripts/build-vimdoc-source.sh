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
  # - strip the "N. " numbering from chapter h1 headings (`# 3. Mark` →
  #   `# Mark`) so panvimdoc's tag slugs don't carry `-3.-` cruft
  # - strip links pointing at *.md targets, keep display text
  # Chapters stay at h1 in the combined doc (no demotion); with no
  # enclosing `# vimficiency` heading, panvimdoc prefixes tags with just
  # the project name (e.g. `vimficiency-mark`) instead of double-wrapping
  # (`vimficiency-vimficiency-3.-mark`).
  sed -E \
    -e '/\*\*\[Index\]/d' \
    -e '/^---$/d' \
    -e 's/^# [0-9]+\. /# /' \
    -e 's/\[([^][]+)\]\(([^)]*\.md[^)]*)\)/\1/g' \
    "$1"
}

{
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
