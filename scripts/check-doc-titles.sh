#!/usr/bin/env bash
# Verify each doc-src/*.md page's frontmatter `title:` matches its
# first H1 heading. Both consumers (Astro Starlight reads frontmatter,
# panvimdoc reads the H1) treat the title as canonical, so drift would
# silently produce different titles on the two surfaces.
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

shopt -s nullglob
fail=0
for f in doc-src/*.md; do
  fm_title=$(awk '
    NR == 1 && /^---$/ { in_fm = 1; next }
    in_fm && /^---$/   { exit }
    in_fm && /^title:/ {
      sub(/^title:[[:space:]]*/, "")
      gsub(/^"|"$/, "")
      print
      exit
    }
  ' "$f")

  h1=$(awk '
    in_fm && /^---$/   { in_fm = 0; next }
    NR == 1 && /^---$/ { in_fm = 1; next }
    in_fm              { next }
    /^# / {
      sub(/^# /, "")
      print
      exit
    }
  ' "$f")

  if [[ -z "$fm_title" ]]; then
    echo "fail: $f has no frontmatter title:" >&2
    fail=1
    continue
  fi
  if [[ -z "$h1" ]]; then
    echo "fail: $f has no H1 heading" >&2
    fail=1
    continue
  fi
  if [[ "$fm_title" != "$h1" ]]; then
    echo "fail: $f title drift" >&2
    echo "  frontmatter: $fm_title" >&2
    echo "  h1:          $h1" >&2
    fail=1
  fi
done

if [[ $fail -eq 0 ]]; then
  echo "ok: all doc-src/*.md frontmatter titles match their H1 headings"
fi
exit "$fail"
