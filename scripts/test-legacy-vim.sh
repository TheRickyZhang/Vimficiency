#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR=build-legacy
cmake -S . -B "$BUILD_DIR" -DVIMFICIENCY_LEGACY_VIM=ON
cmake --build "$BUILD_DIR" -j
"$BUILD_DIR/tests/vimficiency_tests" --gtest_brief=1
echo "All tests passed with VIMFICIENCY_LEGACY_VIM=ON"
