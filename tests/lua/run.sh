#!/usr/bin/env bash
# Run pure-Lua vimficiency tests under fresh headless Neovim instances.
# UI/event-loop tests like simulate.lua are sensitive to shared editor state,
# so each file gets its own process.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
status=0

while IFS= read -r -d '' file; do
  name="$(basename "$file")"
  [[ "$name" == "runner.lua" || "$name" == "simulate_integration.lua" || "$name" == _* ]] && continue
  VF_TEST_FILE="$file" nvim --headless -l "$here/runner.lua" || status=$?
done < <(find "$here" -maxdepth 1 -name '*.lua' -print0 | sort -z)

 nvim --headless -u NONE --cmd 'set noswapfile' -c "luafile $here/simulate_integration.lua" || status=$?

exit "$status"
