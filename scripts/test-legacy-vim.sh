#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD_DIR=build-legacy
cmake -S . -B "$BUILD_DIR" -DVIMF_LEGACY_VIM=ON
cmake --build "$BUILD_DIR" -j
VIMFY_BUILD_DIR="$BUILD_DIR" scripts/vimfy_tests unit
VIMFY_BUILD_DIR="$BUILD_DIR" scripts/vimfy_tests expect
VIMFY_BUILD_DIR="$BUILD_DIR" scripts/vimfy_tests lua
echo "All tests passed with VIMF_LEGACY_VIM=ON"
