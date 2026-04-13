#!/usr/bin/env bash
# Run pure-Lua vimficiency tests under headless Neovim.
# Exit code mirrors the runner: zero iff every test passed.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec nvim --headless -l "$here/runner.lua"
