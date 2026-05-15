#!/usr/bin/env bash
# Run all vimficiency test suites (C++ + FuzzTest seed corpus + Lua). This
# mirrors the main CI correctness steps for contributors who want a single
# command. For focused iteration, prefer the per-suite runners directly:
#   ./build/tests/vimficiency_tests --gtest_filter="..."             (C++)
#   ./build/tests/vimficiency_fuzz_tests --gtest_filter="..."        (FuzzTest)
#   bash tests/lua/run.sh                                            (all Lua)
#   VF_TEST_FILE=path/to/file.lua nvim --headless -l \
#       tests/lua/runner.lua                                         (one Lua file)
#
# Assumes the build artifacts exist (`cmake -B build && cmake --build
# build`) — both the gtest binary and `libvimficiency.so` need to be
# present. The Lua runner loads the `.so` via LuaJIT FFI.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$here/.." && pwd)"

if [[ ! -x "$repo_root/build/tests/vimficiency_tests" ]]; then
  echo "error: build/tests/vimficiency_tests not found" >&2
  echo "  Run: cmake -B build && cmake --build build" >&2
  exit 1
fi

if [[ ! -x "$repo_root/build/tests/vimficiency_fuzz_tests" ]]; then
  echo "error: build/tests/vimficiency_fuzz_tests not found" >&2
  echo "  Run: cmake -B build -DVIMF_ENABLE_FUZZTEST=ON && cmake --build build" >&2
  exit 1
fi

"$repo_root/build/tests/vimficiency_tests" --gtest_brief=1
env FUZZTEST_FUZZ_FOR=0 \
  FUZZTEST_PRNG_SEED=OffQXb8u5_vZtH4-7wgVOLu_HNAhPIbLz7CFF13u3nk \
  "$repo_root/build/tests/vimficiency_fuzz_tests" --gtest_brief=1
bash "$repo_root/tests/lua/run.sh"
