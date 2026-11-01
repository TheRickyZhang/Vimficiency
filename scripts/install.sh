#!/usr/bin/env bash
# Install the native library into build/: download the prebuilt for this
# platform when HEAD is a release tag, otherwise build from source.
# Plugin managers run this as the build step (lazy.nvim `build`, packer `run`,
# vim-plug `do`). Set VIMFICIENCY_BUILD_FROM_SOURCE=1 to skip the download.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

repo="TheRickyZhang/Vimficiency"

asset=""
lib=""
case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)        asset="libvimficiency-linux-x86_64.so";   lib="libvimficiency.so" ;;
  Linux-aarch64)       asset="libvimficiency-linux-aarch64.so";  lib="libvimficiency.so" ;;
  Darwin-arm64)        asset="libvimficiency-macos-arm64.dylib"; lib="libvimficiency.dylib" ;;
esac

try_prebuilt() {
  [[ -z "${VIMFICIENCY_BUILD_FROM_SOURCE:-}" ]] || return 1
  [[ -n "$asset" ]] || return 1
  command -v curl >/dev/null || return 1
  local tag
  tag="$(git describe --tags --exact-match HEAD 2>/dev/null)" || return 1

  local url="https://github.com/$repo/releases/download/$tag/$asset"
  local tmp
  tmp="$(mktemp "${TMPDIR:-/tmp}/vimficiency.XXXXXX")"
  if ! curl -fsSL --retry 2 -o "$tmp" "$url"; then
    rm -f "$tmp"
    echo "vimficiency: no prebuilt at $url"
    return 1
  fi
  mkdir -p build
  mv "$tmp" "build/$lib"
  echo "vimficiency: installed prebuilt $tag ($asset)"
}

if try_prebuilt; then
  exit 0
fi

echo "vimficiency: building from source (Release, library only)"
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVIMF_DEBUG=OFF -DVIMF_BUILD_TESTS=OFF
cmake --build build -j --target vimficiency
