#pragma once

// VimOptions.h - Compile-time options for Vim/Neovim behavior differences,
// plus runtime-configurable shiftwidth.
//
// By default, vimficiency uses NEOVIM defaults (the modern sensible defaults).
// Define VIMFICIENCY_LEGACY_VIM to use traditional Vim defaults instead.
//
// Key differences:
//
// | Option       | Neovim (default) | Vim (LEGACY_VIM)          |
// |--------------|------------------|---------------------------|
// | startofline  | OFF              | ON                        |
// | joinspaces   | OFF              | ON                        |
// | autoindent   | ON               | OFF                       |
// | Y mapping    | y$ (to EOL)      | yy (whole line)           |
//
// startofline affects: gg, G, <C-d>, <C-u>, <C-f>, <C-b>, dd, H, M, L
//   - OFF: cursor preserves column position
//   - ON:  cursor moves to first non-blank character
//
// joinspaces affects: J command
//   - OFF: single space after .!? when joining
//   - ON:  two spaces after .!? when joining

namespace VimOptions {

constexpr bool startOfLine() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

constexpr bool joinSpaces() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

constexpr bool yIsYankLine() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

constexpr bool autoindent() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return false;
#else
    return true;
#endif
}

// Shiftwidth: number of spaces per indent level.
// Affects <BS> in autoindent context (deletes to previous shiftwidth boundary).
// Runtime-configurable via FFI (auto-detected from Neovim's vim.o.shiftwidth).
inline int& shiftwidthRef() {
    static int sw = 8;
    return sw;
}

inline int shiftwidth() {
    return shiftwidthRef();
}

} // namespace VimOptions
