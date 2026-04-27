#!/usr/bin/env bash
# Run pure-Lua Vimfy tests in a single headless Neovim process
# (with a few escape-hatch files run in their own processes; see below).
#
# Main batch: isolation between test files is maintained by
# `reset_state()` in runner.lua, which tears down plugin globals
# (on_key subs, augroup, user command, scratch buffers, plugin
# `package.loaded` entries) before each file after the first. Running
# in one process amortizes the ~150ms Neovim startup cost across all
# files — the whole main suite completes in well under a second instead
# of multiplying by N.
#
# `-u NONE -U NONE` skips the developer's init.{vim,lua} and gvimrc so a
# local config can't leak plugins or options into the test environment —
# otherwise failures would be flaky between machines/CI.
#
# Isolated files (run in their own processes):
#
# * `simulate/integration.lua` — most fragile to shared-state leakage
#   (async coroutines, extmarks, tab/window choreography).
# * `capture/on_key_mapping_probe.lua` — characterization tests that
#   *intentionally* probe Neovim's `vim.on_key` internals. Running them
#   primes Neovim's key-encoding state in ways that break downstream
#   tests relying on the `typed ~= key` heuristic in `key_tracking`
#   (e.g. `map_helper.lua`'s leak-prevention assertions). The probe
#   file is a documentation artifact of Neovim behavior; the
#   contamination is Neovim's, not ours. Fold into the main batch when
#   `key_tracking` stops depending on that heuristic.
#
# The residual ~150ms hit per isolated file is cheap insurance compared
# to debugging a flake later.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

status=0

# Each phase appends a `passed=N failed=N tests=N` line to
# `VF_TEST_SUMMARY_FILE`. We read and sum after all phases complete.
# Routing through a file keeps human stdout uncluttered and avoids
# fragile grep-the-terminal-output logic.
summary="$(mktemp)"
: > "$summary"
export VF_TEST_SUMMARY_FILE="$summary"
trap 'rm -f "$summary"' EXIT

run_phase() {
  local label="$1"
  shift
  printf '%s\n' "[==========] $label"
  "$@" || status=$?
}

run_phase "main batch" nvim --headless -u NONE -U NONE -l "$here/runner.lua"

run_phase "on_key_mapping_probe.lua (isolated)" \
  env VF_TEST_FILE="$here/capture/on_key_mapping_probe.lua" \
  VF_TEST_SUMMARY_FILE="$summary" \
  nvim --headless -u NONE -U NONE -l "$here/runner.lua"

run_phase "simulate/integration.lua (isolated)" \
  nvim --headless -u NONE -U NONE \
  --cmd 'set noswapfile' -c "luafile $here/simulate/integration.lua"

# Aggregate the per-phase summary lines.
grand_passed=0
grand_failed=0
grand_tests=0
while IFS= read -r line; do
  p=$(printf '%s' "$line" | sed -n 's/.*passed=\([0-9]\+\).*/\1/p')
  f=$(printf '%s' "$line" | sed -n 's/.*failed=\([0-9]\+\).*/\1/p')
  t=$(printf '%s' "$line" | sed -n 's/.*tests=\([0-9]\+\).*/\1/p')
  grand_passed=$((grand_passed + ${p:-0}))
  grand_failed=$((grand_failed + ${f:-0}))
  grand_tests=$((grand_tests + ${t:-0}))
done < "$summary"

printf '\n'
# `N/N passed` is unambiguous on full-pass runs — the numerator === the
# denominator means nothing failed. On failures the line reads
# "X/N passed, Y FAILED" so the failure count is hard to miss.
if [[ $grand_failed -eq 0 ]]; then
  printf '[==========] %d/%d tests passed across 3 processes.\n' \
    "$grand_passed" "$grand_tests"
else
  printf '[==========] %d/%d tests passed across 3 processes — %d FAILED.\n' \
    "$grand_passed" "$grand_tests" "$grand_failed"
fi

exit "$status"
