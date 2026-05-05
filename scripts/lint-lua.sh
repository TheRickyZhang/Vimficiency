#!/usr/bin/env bash
# Run lua-language-server in batch-check mode over every Lua file in the repo.
# Mirrors the LSP's editor-time behavior so warnings can be surveyed from the
# CLI.
#
# Resolves the binary in this order:
#   1. anything on PATH (system package, manual symlink, etc.)
#   2. Mason's installation (~/.local/share/nvim/mason/packages/...)
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"

luals="$(command -v lua-language-server || true)"
if [[ -z "$luals" ]]; then
  mason_luals="$HOME/.local/share/nvim/mason/packages/lua-language-server/lua-language-server"
  if [[ -x "$mason_luals" ]]; then
    luals="$mason_luals"
  else
    echo "error: lua-language-server not found on PATH or in Mason" >&2
    echo "install via: pacman -S lua-language-server  (or :MasonInstall lua-language-server)" >&2
    exit 1
  fi
fi

# LuaLS requires --logpath even though --check_format=pretty (the default)
# writes diagnostics to stdout. The logpath just collects internal logs.
exec "$luals" \
  --check "$repo_root" \
  --configpath "$repo_root/.luarc.json" \
  --logpath "${LUALS_LOG_DIR:-/tmp/luals-check}"
