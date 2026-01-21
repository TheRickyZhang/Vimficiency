#pragma once

#include <string>

#include "Editor/Mode.h"
#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Editor/LineRange.h"
#include "Utils/Lines.h"

// Edit operations that modify buffer content.
//
// Design principles:
// - Assume valid state (use assertions, not defensive clamping)
// - Minimal API (single-line ops take string& + int&, not Lines& + Position&)
// - No redundant wrappers (inline trivial operations at call sites)

namespace VimCore {

// =============================================================================
// Position Clamping Helpers
// =============================================================================

// Clamp col to valid normal mode range: [0, line.size()-1] or 0 if empty
inline void clampCol(const std::string& line, int& col) {
  col = line.empty() ? 0 : std::min(col, static_cast<int>(line.size()) - 1);
}

// Clamp col to valid insert mode range: [0, line.size()] (cursor can be after last char)
inline void clampInsertCol(const std::string& line, int& col) {
  col = std::min(col, static_cast<int>(line.size()));
}

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

// Delete text in character range. Modifies lines and updates pos.
// All ranges are inclusive (both start and end positions are deleted).
// Mode determines position clamping: Normal clamps to last char, Insert allows after last char
// Empty line removal follows Vim behavior:
//   - Cursor on same line as deletion (e.g., D at col 0): keep empty line
//   - Cursor on different line (e.g., db from col 0): remove empty line
void deleteRange(Lines& lines, const Range& range, Position& pos, Mode mode = Mode::Normal);

// Delete entire lines. Modifies lines and updates pos.
// Pos goes to first non-blank of the line following the deleted range.
void deleteRangeLinewise(Lines& lines, const LineRange& range, Position& pos);

// Insert text at position. Handles newlines (splits into multiple lines).
// After insert, pos is at end of inserted text.
void insertText(Lines& lines, Position& pos, const std::string& text);

// J/gJ - join current line with next
// addSpace=true: strip trailing/leading whitespace, add single space (J)
// addSpace=false: simple concatenation (gJ)
void joinLines(Lines& lines, Position& pos, bool addSpace);

// o - open new line below current, pos moves to new line col 0
// Precondition: lines not empty
void openLineBelow(Lines& lines, Position& pos);

// O - open new line above current, pos moves to new line col 0
// Precondition: lines not empty
void openLineAbove(Lines& lines, Position& pos);

} // namespace VimCore
