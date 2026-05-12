#!/usr/bin/env bash
# Point this repo's hooks at .githooks/. Run once after cloning.
#
# Mainly wires up the pre-push hook that backgrounds bench-local-run.sh —
# see .githooks/pre-push for behavior.
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

chmod +x .githooks/* scripts/bench-local-run.sh

git config core.hooksPath .githooks
echo "core.hooksPath -> .githooks"
echo "hooks installed:"
ls -1 .githooks
