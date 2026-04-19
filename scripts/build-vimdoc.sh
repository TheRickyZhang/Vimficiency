#!/usr/bin/env bash
# End-to-end vimdoc build: doc-src/*.md → build/vimficiency.md →
# doc/vimficiency.txt. Mirrors the CI workflow (.github/workflows/vimdoc.yml)
# for local iteration. Requires panvimdoc checked out somewhere (set
# PANVIMDOC_DIR, default /tmp/panvimdoc).
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
panvimdoc_dir="${PANVIMDOC_DIR:-/tmp/panvimdoc}"

if [[ ! -f "$panvimdoc_dir/panvimdoc.sh" ]]; then
  echo "error: panvimdoc not found at $panvimdoc_dir" >&2
  # Pin to the same tag CI uses (.github/workflows/vimdoc{,-check}.yml →
  # `kdheepak/panvimdoc@v4.0.1`) so local output matches byte-for-byte.
  echo "clone it: git clone --depth 1 --branch v4.0.1 https://github.com/kdheepak/panvimdoc $panvimdoc_dir" >&2
  exit 1
fi

bash "$repo_root/scripts/build-vimdoc-source.sh"

"$panvimdoc_dir/panvimdoc.sh" \
  --project-name vimficiency \
  --input-file "$repo_root/build/vimficiency.md" \
  --vim-version "Neovim >= 0.9"

bash "$repo_root/scripts/postprocess-vimdoc.sh"
