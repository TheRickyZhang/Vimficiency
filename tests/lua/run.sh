#!/usr/bin/env bash
# Run pure-Lua vimficiency tests under fresh headless Neovim instances.
# UI/event-loop tests like simulate/integration.lua are sensitive to shared
# editor state, so each file gets its own process. Discovery recurses into
# subdirs so the layout mirrors lua/vimficiency/ (session/, capture/, simulate/).
#
# `-u NONE -U NONE` skips the developer's init.{vim,lua} and gvimrc so a
# local config can't leak plugins or options into the test environment —
# otherwise failures would be flaky between machines/CI.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
status=0

while IFS= read -r -d '' file; do
  name="$(basename "$file")"
  rel="${file#$here/}"
  [[ "$name" == "runner.lua" || "$name" == _* || "$rel" == "simulate/integration.lua" ]] && continue
  VF_TEST_FILE="$file" nvim --headless -u NONE -U NONE -l "$here/runner.lua" || status=$?
done < <(find "$here" -name '*.lua' -print0 | sort -z)

nvim --headless -u NONE -U NONE --cmd 'set noswapfile' -c "luafile $here/simulate/integration.lua" || status=$?

exit "$status"
