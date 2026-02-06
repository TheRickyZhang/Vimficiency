#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR=build-release
cmake -S . -B "$BUILD_DIR" -DVIMFICIENCY_DEBUG=OFF
cmake --build "$BUILD_DIR" -j
"$BUILD_DIR/tests/vimficiency_tests" --gtest_brief=1
echo "All tests passed with VIMFICIENCY_DEBUG=OFF"
