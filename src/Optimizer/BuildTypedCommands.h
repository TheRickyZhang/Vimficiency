#pragma once

#include "Keyboard/KeyedSequence.h"
#include "Utils/Lines.h"
#include "Utils/StringUtils.h"
#include "VimCore/VimOptions.h"

#include <string_view>

// Build the typed content string from goalLines, accounting for Neovim autoindent.
//
// When autoindent is active, entering insert mode via cc/c{motion}/o copies
// leading whitespace from the source line. Each subsequent <CR> copies indent
// from the current line. We handle autoindent by:
//   - Stripping it if the goal line starts with the same indent
//   - Using <BS> to remove excess spaces if autoindent is longer than goal's indent
//   - Using <C-u> to clear the line if there's no prefix relationship
//
// Parameters:
//   goalLines          - the lines to type out
//   initialAutoindent  - indent provided on the first typed line (e.g. from cc's source line)
//   linePrefix         - text before edit region on first line (for computing line 1+ autoindent)
//   suffixLeadingSpaces - whitespace to restore on last line (stripped from suffix by <CR>)
//
// Returns KeyedSequence including trailing <Esc>.
inline KeyedSequence buildTypedCommands(
    const Lines &goalLines,
    std::string_view initialAutoindent = "",
    std::string_view linePrefix = "",
    int suffixLeadingSpaces = 0) {
  KeyedSequence ks;

  // Helper: emit keys for a line given expected autoindent
  // Cases: (1) no autoindent, (2) goal matches, (3) goal has less indent, (4) mismatch
  auto emitLine = [&](std::string_view line, std::string_view autoindent) {
    // Case 1: no autoindent — type full line
    if (autoindent.empty()) {
      ks.appendText(line);
      return;
    }

    // Case 2: goal starts with autoindent — strip it
    if (line.size() >= autoindent.size() &&
        line.substr(0, autoindent.size()) == autoindent) {
      ks.appendText(line.substr(autoindent.size()));
      return;
    }

    // Case 3: autoindent starts with goal's indent — use <BS> if more efficient
    // <BS> in autoindent deletes to previous shiftwidth boundary, not just 1 space.
    auto goalIndent = leadingWhitespace(line);
    if (autoindent.size() > goalIndent.size() &&
        autoindent.substr(0, goalIndent.size()) == goalIndent) {
      int bsNeeded = bsCountForIndent(
          static_cast<int>(autoindent.size()),
          static_cast<int>(goalIndent.size()),
          VimOptions::shiftwidth());
      if (bsNeeded >= 0) {
        int remainder = static_cast<int>(line.size() - goalIndent.size());
        // <BS> is better when: bsNeeded + remainder < 2 + line.size()
        if (bsNeeded + remainder < 2 + static_cast<int>(line.size())) {
          ks.appendRepeated(KeyedSequence::BS, bsNeeded);
          ks.appendText(line.substr(goalIndent.size()));
          return;
        }
      }
    }

    // Case 4: mismatch — clear with <C-u> and type full line
    ks += KeyedSequence::CtrlU;
    ks.appendText(line);
  };

  // Compute what autoindent Neovim provides for a given line during insert-mode typing.
  // Line 0: uses initialAutoindent (from cc source line or mode-entry command).
  // Line 1 with non-empty linePrefix: autoindent from prefix + goalLines[0].
  // Line 2+: autoindent from previous goal line.
  auto autoindentFor = [&](size_t i) -> std::string_view {
    if (i == 0) return initialAutoindent;

    if (i == 1 && !linePrefix.empty()) {
      auto prefixWs = leadingWhitespace(linePrefix);
      if (prefixWs.size() == linePrefix.size()) {
        // prefix is entirely spaces — combined indent
        auto goalWs = leadingWhitespace(goalLines[i - 1]);
        thread_local std::string combinedIndent;
        combinedIndent.assign(linePrefix.size() + goalWs.size(), ' ');
        return combinedIndent;
      }
      return prefixWs;
    }

    return leadingWhitespace(goalLines[i - 1]);
  };

  // Main loop: handle all lines uniformly via helpers
  if constexpr (VimOptions::autoindent()) {
    for (size_t i = 0; i < goalLines.size(); i++) {
      if (i > 0) ks += KeyedSequence::CR;
      emitLine(goalLines[i], autoindentFor(i));
    }
  } else {
    // autoindent off — type full lines directly
    for (size_t i = 0; i < goalLines.size(); i++) {
      if (i > 0) ks += KeyedSequence::CR;
      ks.appendText(goalLines[i]);
    }
  }

  // On the last typed line, restore suffix leading whitespace that was stripped
  // by <CR> autoindent behavior. Only needed for multi-line edits where the suffix
  // has leading spaces (char-wise edits that span lines).
  if constexpr (VimOptions::autoindent()) {
    if (suffixLeadingSpaces > 0 && goalLines.size() > 1) {
      ks.appendChar(' ', suffixLeadingSpaces);
    }
  }

  ks += KeyedSequence::Esc;
  return ks;
}
