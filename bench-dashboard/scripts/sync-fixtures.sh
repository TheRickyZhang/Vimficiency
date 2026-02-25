#!/usr/bin/env bash
set -euo pipefail

DASH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FIXTURES_DIR="$DASH_DIR/fixtures"
PUBLIC_DIR="$DASH_DIR/public"

TARGETS=(edit motion composition tests)

for target in "${TARGETS[@]}"; do
  mkdir -p "$PUBLIC_DIR/$target"
  cp "$FIXTURES_DIR/$target/data.json" "$PUBLIC_DIR/$target/data.json"

  if [[ -f "$FIXTURES_DIR/$target/explore.json" ]]; then
    cp "$FIXTURES_DIR/$target/explore.json" "$PUBLIC_DIR/$target/explore.json"
  else
    rm -f "$PUBLIC_DIR/$target/explore.json"
  fi

done

echo "Synced dashboard fixtures into public/."
