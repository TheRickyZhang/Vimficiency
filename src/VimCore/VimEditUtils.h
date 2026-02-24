#pragma once

#include <string>
#include <string_view>

#include "Types/Mode.h"
#include "Types/CursorPos.h"
#include "Types/Range.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"

// Edit operations that modify buffer content.
//
// Design principles:
// - Assume valid state (use assertions, not defensive clamping)
// - Minimal API (single-line ops take string& + int&, not Lines& + CursorPos&)
// - No redundant wrappers (inline trivial operations at call sites)

namespace VimCore {

// =============================================================================
// CursorPos Clamping Helpers
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
// Exclusive Motion Range Adjustment  (see :help exclusive-linewise)
// =============================================================================
//
// Vim's exclusive motions (), (), {, }) produce half-open ranges [begin, end)
// where `end` is the character NOT included in the operation.  When that
// exclusive end lands at column 0 of a different line, Vim applies special
// rules before executing the operator (d, c, y, …):
//
//   Rule 1 – LINEWISE
//     Condition: begin.col == 0  AND  end.col == 0,  on different lines
//     Effect:    the operator is promoted to linewise — whole lines are
//                removed including their trailing newlines.
//     Vim docs:  "An exclusive motion used for deletion that starts in
//                column 1 and ends at column 1 of another line, will be
//                made linewise."
//     Example:   d) from (0,0) on ["End.", "Start"]
//                  motion endpoint = (1,0)  →  linewise delete of line 0
//                  result: ["Start"],  cursor (0,0)
//
//   Rule 2 – BACKED UP
//     Condition: begin.col > 0  AND  end.col == 0,  on different lines
//     Effect:    end moves backward to end-of-previous-line.  The newline
//                between the two lines is NOT deleted (it is preserved).
//     Vim docs:  "If the motion is exclusive and the end of the motion is
//                in column 1, the end of the motion is moved to the end of
//                the previous line and the motion becomes inclusive."
//     Example:   d) from (0,2) on ["be.df.", ".ee  cb"]
//                  motion endpoint = (1,0)  →  backed up to (0,6)
//                  deletes ".df." (cols 2–5), newline stays
//                  result: ["be", ".ee  cb"],  cursor (0,1)
//
// For BACKWARD motions (d(, d{), the exclusive end is the CURSOR position
// (the higher end of the range), not the motion destination.
//     Example:   d( from (1,0) on ["End. xyz", "abc"]
//                  ( goes to (0,5),  range = [(0,5), (1,0))
//                  cursor (1,0) is at col 0  →  backed up to (0,8)
//                  deletes "xyz" (cols 5–7), newline stays
//                  result: ["End. ", "abc"],  cursor (0,4)
//
// Callers:
//   - EditInterpreter.cpp:  d)/c), d(/c(, d}/c}, d{/c{
//   - EditExplorer.cpp:     exploreSentenceEdits (both directions)
//
// Operator-specific notes:
//   - d with Linewise: use deleteRangeLinewise() for proper cursor placement.
//   - c with Linewise: Vim forces characterwise — use deleteRange() instead.
//     (see :help exclusive-linewise — "For the 'c' command the operator is
//     not strung out but instead is made characterwise.")
//   - d} has a paragraph-specific EOF exception: when } reaches the last
//     non-blank line, the motion becomes inclusive (position is ON the last
//     char, not past it).  This is handled before calling adjustExclusiveRange.
//
// Oracle-verified tests: ExclusiveLineAdjust_* in ManualTest.cpp.
//

enum class ExclusiveAdjust {
  None,      // end was not at col 0, or same line — range unchanged
  Linewise,  // both begin and end at col 0 — caller should use linewise delete
  BackedUp,  // end backed up to end of previous line — range modified in place
};

// Adjust a half-open [begin, end) range per Vim's exclusive-linewise rule.
// Modifies `range.end` in place for BackedUp; leaves it unchanged for
// Linewise and None.  Returns the adjustment type so callers can dispatch
// (e.g., deleteRangeLinewise for Linewise, deleteRange for BackedUp/None).
inline ExclusiveAdjust adjustExclusiveRange(Range& range, const Lines& lines) {
  if (range.end.col == 0 && range.end.line > range.begin.line) {
    if (range.begin.col == 0) return ExclusiveAdjust::Linewise;
    range.end = CursorPos(range.end.line - 1,
                         static_cast<int>(lines[range.end.line - 1].size()));
    return ExclusiveAdjust::BackedUp;
  }
  return ExclusiveAdjust::None;
}

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

// Delete text in character range. Modifies lines and updates pos.
// Range is half-open [begin, end).
// Mode determines position clamping: Normal clamps to last char, Insert allows after last char
// Empty line removal follows Vim behavior:
//   - Cursor on same line as deletion (e.g., D at col 0): keep empty line
//   - Cursor on different line (e.g., db from col 0): remove empty line
void deleteRange(Lines& lines, const Range& range, CursorPos& pos, Mode mode = Mode::Normal);

// Delete entire lines in [beginLine, endLine). Modifies lines and updates pos.
// Pos goes to first non-blank of the line following the deleted range.
// hasLinesBelow: if true, cursor is not clamped when deletion includes the last line,
// because the real buffer has lines below that the cursor would land on.
void deleteRangeLinewise(Lines& lines, const LineRange& range, CursorPos& pos,
                         bool hasLinesBelow = false);

// Insert text at position. Handles newlines (splits into multiple lines).
// After insert, pos is at end of inserted text.
void insertText(Lines& lines, CursorPos& pos, std::string_view text);

// J/gJ - join current line with next
// addSpace=true: strip trailing/leading whitespace, add single space (J)
// addSpace=false: simple concatenation (gJ)
void joinLines(Lines& lines, CursorPos& pos, bool addSpace);

// o - open new line below current, pos moves to new line col 0
// Precondition: lines not empty
void openLineBelow(Lines& lines, CursorPos& pos);

// O - open new line above current, pos moves to new line col 0
// Precondition: lines not empty
void openLineAbove(Lines& lines, CursorPos& pos);

} // namespace VimCore
