#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

is_known_module() {
  case "$1" in
    VimTypes|Optimizer|Interpreter|Session|Effort|Keyboard|VimCore|Boundary|Utils) return 0 ;;
    *) return 1 ;;
  esac
}

is_allowed_edge() {
  case "$1" in
    "Optimizer->VimTypes"|"Optimizer->Utils"|"Optimizer->Keyboard"|"Optimizer->VimCore"|"Optimizer->Boundary"|"Optimizer->Interpreter"|"Optimizer->Effort") return 0 ;;
    "Effort->Keyboard") return 0 ;;
    "Interpreter->VimTypes"|"Interpreter->VimCore"|"Interpreter->Keyboard"|"Interpreter->Utils") return 0 ;;
    "Session->VimTypes") return 0 ;;
    "VimCore->VimTypes"|"VimCore->Utils"|"VimCore->Boundary") return 0 ;;
    "Keyboard->VimTypes"|"Keyboard->Utils") return 0 ;;
    "Boundary->VimTypes"|"Boundary->Utils") return 0 ;;
    "Utils->VimTypes") return 0 ;;
    *) return 1 ;;
  esac
}

declare -A seen_counts=()
declare -A bad_counts=()
declare -A bad_details=()

while IFS=$'\t' read -r src_mod dst_mod location include_path; do
  if ! is_known_module "$src_mod"; then
    continue
  fi
  if ! is_known_module "$dst_mod"; then
    continue
  fi
  if [[ "$src_mod" == "$dst_mod" ]]; then
    continue
  fi

  edge="${src_mod}->${dst_mod}"
  seen_counts["$edge"]=$(( ${seen_counts["$edge"]:-0} + 1 ))

  if ! is_allowed_edge "$edge"; then
    bad_counts["$edge"]=$(( ${bad_counts["$edge"]:-0} + 1 ))
    bad_details["$edge"]+=$'\n'"  - ${location} includes \"${include_path}\""
  fi
done < <(
  rg -n '#include "([^"]+)"' src | awk '
    match($0, /^([^:]+):([0-9]+):#include "([^"]+)"/, m) {
      file = m[1];
      line = m[2];
      inc  = m[3];
      split(file, fparts, "/");
      src_mod = fparts[2];
      split(inc, iparts, "/");
      dst_mod = iparts[1];
      printf "%s\t%s\t%s:%s\t%s\n", src_mod, dst_mod, file, line, inc;
    }
  '
)

echo "Observed cross-module edges:"
if [[ ${#seen_counts[@]} -eq 0 ]]; then
  echo "  (none)"
else
  {
    for edge in "${!seen_counts[@]}"; do
      printf "%5d %s\n" "${seen_counts[$edge]}" "$edge"
    done
  } | sort -k2,2
fi

if [[ ${#bad_counts[@]} -gt 0 ]]; then
  echo
  echo "Disallowed module dependencies found:"
  {
    for edge in "${!bad_counts[@]}"; do
      printf "%5d %s\n" "${bad_counts[$edge]}" "$edge"
      printf "%s\n" "${bad_details[$edge]}"
    done
  } | sort -k2,2
  exit 1
fi

echo
echo "Module dependency lint passed."
