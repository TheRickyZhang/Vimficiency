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
  # Drop YAML frontmatter (delimited by --- ... --- at file start). The
  # frontmatter exists for the Astro Starlight site (`docs-site/`) and
  # is noise to panvimdoc — leaking `title:`, etc. into the body would
  # render as plain text in `:help`. The awk block below strips lines
  # from the first `^---$` to the matching second `^---$` only when
  # the first one is line 1, so legitimate `---` thematic breaks in
  # the body survive.
  awk '
    NR == 1 && /^---$/ { in_fm = 1; next }
    in_fm && /^---$/   { in_fm = 0; next }
    in_fm              { next }
    { print }
  ' "$1" | sed -E \
    -e 's/\[([^][]+)\]\(([^)]*\.md[^)]*)\)/\1/g'
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
