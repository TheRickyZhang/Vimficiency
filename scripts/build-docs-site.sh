#!/usr/bin/env bash
# Builds the planner wasm module and the docs site, then verifies the output.
# Shared by vimdoc-check.yml and bench-local-run.sh; arguments are passed to the
# wasm configure step.
set -euo pipefail

cd "$(dirname "$0")/.."

emcmake cmake -B build-wasm -DVIMF_WASM=ON -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build-wasm --target vimfy_planner_wasm
mkdir -p web/docs/public/wasm
cp build-wasm/wasm/vimfy_planner.js build-wasm/wasm/vimfy_planner.wasm web/docs/public/wasm/

( cd web/docs && bun install --frozen-lockfile && bun run build )

test -f web/docs/dist/index.html
test -f web/docs/dist/01-installation/index.html
test -f web/docs/dist/how-it-works/index.html
bun scripts/smoke-planner-wasm.ts web/docs/dist/wasm
