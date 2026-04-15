#!/usr/bin/env bash
# Post-process doc/vimficiency.txt after panvimdoc runs.
# Currently: add a *vimficiency* tag to the header so `:help vimficiency`
# resolves directly (panvimdoc only emits *vimficiency.txt*).
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
target="$repo_root/doc/vimficiency.txt"

if [[ ! -f "$target" ]]; then
  echo "error: $target not found (did panvimdoc run?)" >&2
  exit 1
fi

# Only add the tag if it isn't already there (idempotent).
if ! grep -q '^\*vimficiency\.txt\*  \*vimficiency\*' "$target"; then
  sed -i '1s|^\*vimficiency\.txt\*$|*vimficiency.txt*  *vimficiency*|' "$target"
fi

echo "Post-processed $target"
