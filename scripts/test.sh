#!/usr/bin/env bash
# Compatibility wrapper for the CI-like fast test gate.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

"$here/vimfy_tests" "$@"
