#!/usr/bin/env bash
set -euo pipefail

FILES=(
  "tests/MotionOptimizer/HumanApprovalTest.cpp"
  "tests/EditOptimizer/HumanApprovalTest.cpp"
  "tests/CompositionOptimizer/HumanApprovalTest.cpp"
)

missing=0

for f in "${FILES[@]}"; do
  if ! grep -q "TODO(HUMAN_APPROVAL_HARDEN)" "$f"; then
    echo "Missing TODO(HUMAN_APPROVAL_HARDEN) tag: $f"
    missing=1
  fi
done

if [[ $missing -ne 0 ]]; then
  echo "HumanApproval TODO audit tags are missing."
  exit 1
fi

echo "OK: HumanApproval TODO audit tags present."
