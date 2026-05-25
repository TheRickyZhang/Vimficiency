#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR=build-release
cmake -S . -B "$BUILD_DIR" -DVIMF_DEBUG=OFF
cmake --build "$BUILD_DIR" -j
VIMFY_BUILD_DIR="$BUILD_DIR" scripts/vimfy_tests
echo "All tests passed with VIMF_DEBUG=OFF"
