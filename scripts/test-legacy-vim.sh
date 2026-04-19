#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR=build-legacy
cmake -S . -B "$BUILD_DIR" -DVIMF_LEGACY_VIM=ON
cmake --build "$BUILD_DIR" -j
"$BUILD_DIR/tests/vimficiency_tests" --gtest_brief=1
VIMFICIENCY_LIB_PATH="$PWD/$BUILD_DIR/libvimficiency.so" bash tests/lua/run.sh
echo "All tests passed with VIMF_LEGACY_VIM=ON"
