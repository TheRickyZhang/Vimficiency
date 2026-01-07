#pragma once

// VimOptions.h - Compile-time options for Vim/Neovim behavior differences
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

// Returns true if startofline behavior should be used
// (go to first non-blank on line-changing commands)
constexpr bool startOfLine() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

// Returns true if joinspaces behavior should be used
// (two spaces after .!? when joining lines)
constexpr bool joinSpaces() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

// Returns true if Y should behave like yy (yank line) instead of y$ (yank to EOL)
constexpr bool yIsYankLine() {
#ifdef VIMFICIENCY_LEGACY_VIM
    return true;
#else
    return false;
#endif
}

} // namespace VimOptions
