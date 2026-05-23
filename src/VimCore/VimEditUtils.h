#pragma once

#include <algorithm>
#include <string>
#include <string_view>

#include "Types/Mode.h"
#include "Types/CharLineRange.h"
#include "Types/LineCharRange.h"
#include "Types/CursorPos.h"
#include "Types/CharRange.h"
#include "Types/LineRange.h"
#include "Types/Lines.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimOptions.h"

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

// Convert an inclusive character position into a same-line half-open endpoint.
inline CursorPos onePastOnSameLine(const Lines& lines, const CursorPos& inclusivePos) {
  int lineLen = static_cast<int>(lines[inclusivePos.line].size());
  return CursorPos(inclusivePos.line, std::min(inclusivePos.col + 1, lineLen));
}

// Linewise change with autoindent: shared mechanics for cc/S, cj, and ck.
//
// Replaces the inclusive line range [firstLine, lastLine] with a single new
// line carrying `sourceLine`'s leading indent (preserved verbatim via
// `leadingIndent`, including tabs), positions the cursor at the end of that
// indent, and switches mode to Insert. Mirrors what Neovim does on a linewise
// change operator: collapse to one autoindented line and drop into insert.
//
// Callers:
//   - cc/S: firstLine == lastLine == pos.line, sourceLine == lines[pos.line]
//   - cj:   firstLine == pos.line, lastLine == pos.line + count, sourceLine == lines[pos.line]
//   - ck:   firstLine == pos.line - count, lastLine == pos.line, sourceLine == lines[firstLine]
//
// Caller is responsible for validating the range (the operator-level
// preconditions like "cj requires count lines below") before invoking.
// Leading indent of a line for Vim's autoindent replay (both spaces and tabs).
// Vim's autoindent copies the source line's indent characters verbatim when
// entering insert mode via cc/S/cj/ck/o/O, including tabs.
//
// Distinct from `leadingWhitespace` in `Optimizer/Indentation.h`, which is
// spaces-only because the optimizer's downstream math (BuildTypedCommands'
// `bsCountForIndent`, the combined-indent assembly in spaces) assumes
// expandtab-style indentation throughout. Don't unify the two — use this one
// from the replay/edit path and the optimizer's one from the goal-shape path.
inline std::string_view leadingIndent(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
  return s.substr(0, i);
}

inline void linewiseChangeWithAutoindent(Lines& lines,
                                         CursorPos& pos,
                                         Mode& mode,
                                         int firstLine,
                                         int lastLine,
                                         std::string_view sourceLine) {
  std::string indent;
  if constexpr (VimOptions::autoindent()) {
    indent = std::string(leadingIndent(sourceLine));
  }
  lines.erase(lines.begin() + firstLine, lines.begin() + lastLine + 1);
  lines.insert(lines.begin() + firstLine, indent);
  pos.line = firstLine;
  pos.setCol(static_cast<int>(indent.size()));
  mode = Mode::Insert;
}

// =============================================================================
// Exclusive Motion CharRange Resolution  (see :help exclusive-linewise)
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
//   - TransformExplorer.cpp:     exploreSentenceEdits (both directions)
//
// Operator-specific notes:
//   - d with Linewise: use deleteLineRangeAndUpdatePos() for proper cursor placement.
//   - c with Linewise: Vim forces characterwise, preserving the line break by
//     backing the range up to the previous line's end.
//   - d} has a paragraph-specific EOF exception: when } reaches the last
//     non-blank line, the motion becomes inclusive (position is ON the last
//     char, not past it).  This is handled before calling
//     resolveExclusiveDeleteRange().
//
// Oracle-verified tests: ExclusiveLineAdjust_* and ChangeMotionsTest.
//

enum class ResolvedDeleteRangeKind {
  Characterwise,
  CharLine,
  LineChar,
  Linewise,
};

struct ResolvedDeleteRange {
  ResolvedDeleteRangeKind kind;
  CharRange charRange;
  CharLineRange charLineRange;
  LineCharRange lineCharRange;
  LineRange lineRange;

  static ResolvedDeleteRange characterwise(CharRange range) {
    return {ResolvedDeleteRangeKind::Characterwise, range,
            CHAR_LINE_RANGE_OUTSIDE_BOUNDARY,
            LINE_CHAR_RANGE_OUTSIDE_BOUNDARY,
            LineRange(0, 0)};
  }

  static ResolvedDeleteRange charLine(CharLineRange range) {
    return {ResolvedDeleteRangeKind::CharLine, CharRange(),
            range, LINE_CHAR_RANGE_OUTSIDE_BOUNDARY, LineRange(0, 0)};
  }

  static ResolvedDeleteRange lineChar(LineCharRange range) {
    return {ResolvedDeleteRangeKind::LineChar, CharRange(),
            CHAR_LINE_RANGE_OUTSIDE_BOUNDARY, range, LineRange(0, 0)};
  }

  static ResolvedDeleteRange linewise(LineRange range) {
    return {ResolvedDeleteRangeKind::Linewise, CharRange(),
            CHAR_LINE_RANGE_OUTSIDE_BOUNDARY,
            LINE_CHAR_RANGE_OUTSIDE_BOUNDARY, range};
  }
};

// Resolve a raw exclusive [begin, end) range into an explicit characterwise
// CharRange or linewise LineRange. `allowLinewise` should be false for
// change-like operators, which keep newline-preserving characterwise behavior
// in the col-0 case.
inline ResolvedDeleteRange resolveExclusiveDeleteRange(
    CharRange range, const Lines& lines, bool allowLinewise) {
  range.normalize();
  if (range.end.col != 0 || range.end.line <= range.begin.line) {
    if (range.begin.col == 0 && range.begin.line < range.end.line) {
      return ResolvedDeleteRange::lineChar(LineCharRange(range.begin.line, range.end));
    }
    return ResolvedDeleteRange::characterwise(range);
  }

  if (range.begin.col == 0) {
    if (allowLinewise) {
      return ResolvedDeleteRange::linewise(LineRange(range.begin.line, range.end.line));
    }
    return ResolvedDeleteRange::characterwise(
        CharRange(range.begin,
                  CursorPos(range.end.line - 1,
                            static_cast<int>(lines[range.end.line - 1].size()))));
  }

  range.end = CursorPos(range.end.line - 1,
                        static_cast<int>(lines[range.end.line - 1].size()));
  return ResolvedDeleteRange::characterwise(range);
}

// =============================================================================
// Backward Exclusive CharRange Construction
// =============================================================================
//
// Backward exclusive motions (db, dB) produce ranges whose exclusive end is
// the cursor position. The characterwise fallback preserves the current line;
// Vim's delete-specific blank-prefix rule is resolved separately below.
//
// contentStartCol: offset for effective-line coordinate systems (e.g.,
// leftColOffset on line 0 in TransformExplorer). Defaults to 0 for real buffers.

inline CharRange buildBackwardExclusiveCharRange(
    const CursorPos& endpoint, const CursorPos& cursor, const Lines& lines,
    int contentStartCol = 0) {
  if (cursor.col == contentStartCol && endpoint.line < cursor.line) {
    int prevLine = cursor.line - 1;
    return CharRange(endpoint, CursorPos(prevLine, static_cast<int>(lines[prevLine].size())));
  }
  return CharRange(endpoint, cursor);
}

inline bool hasOnlyBlankPrefix(std::string_view line, int endCol, int contentStartCol = 0) {
  int begin = std::clamp(contentStartCol, 0, static_cast<int>(line.size()));
  int end = std::clamp(endCol, begin, static_cast<int>(line.size()));
  for (int col = begin; col < end; col++) {
    char c = line[col];
    if (c != ' ' && c != '\t') return false;
  }
  return true;
}

inline ResolvedDeleteRange resolveBackwardExclusiveWordDeleteRange(
    const CursorPos& endpoint, const CursorPos& cursor, const Lines& lines,
    int contentStartCol = 0) {
  if (cursor.col == contentStartCol && endpoint.line < cursor.line) {
    int endpointContentStart = endpoint.line == 0 ? contentStartCol : 0;
    if (endpointContentStart == 0 &&
        hasOnlyBlankPrefix(lines[endpoint.line], endpoint.col, endpointContentStart)) {
      return ResolvedDeleteRange::linewise(LineRange(endpoint.line, cursor.line));
    }
  }

  return ResolvedDeleteRange::characterwise(
      buildBackwardExclusiveCharRange(endpoint, cursor, lines, contentStartCol));
}

// Given a normalized or unnormalized character delete range plus the line-count
// before/after deletion, report whether the line containing range.begin was
// removed in addition to the range's baseline line collapse.
inline bool didDeleteRangeRemoveBeginLine(CharRange range,
                                          int oldLineCount,
                                          int newLineCount) {
  range.normalize();
  int baselineRemoved = range.end.line - range.begin.line;
  return oldLineCount - newLineCount > baselineRemoved;
}

inline void adjustCursorAfterBackwardWordDelete(CharRange range,
                                                int oldLineCount,
                                                const CursorPos& originalPos,
                                                const Lines& lines,
                                                CursorPos& pos,
                                                int firstContentCol = 0) {
  range.normalize();

  int cursorContentStart = (originalPos.line == 0) ? firstContentCol : 0;
  if (originalPos.col != cursorContentStart || originalPos.line <= range.begin.line) return;
  if (!didDeleteRangeRemoveBeginLine(
          range, oldLineCount, static_cast<int>(lines.size()))) {
    return;
  }
  if (lines[pos.line].empty()) return;

  pos.setCol(firstNonBlankColInLine(lines[pos.line]));
}

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

// Delete text in character range. Modifies lines and updates pos.
// CharRange is half-open [begin, end).
// Mode determines position clamping: Normal clamps to last char, Insert allows after last char
// Empty line removal may depend on whether the original cursor was already on
// the range's anchor line, since this API combines deletion with cursor updates.
void deleteRangeAndUpdatePos(
    Lines& lines, const CharRange& range, CursorPos& pos, Mode mode = Mode::Normal);
void deleteCharLineRangeAndUpdatePos(
    Lines& lines, const CharLineRange& range, CursorPos& pos, Mode mode = Mode::Normal);
void deleteLineCharRangeAndUpdatePos(
    Lines& lines, const LineCharRange& range, CursorPos& pos, Mode mode = Mode::Normal);

// Delete entire lines in [beginLine, endLine). Modifies lines and updates pos.
// Pos goes to first non-blank of the line following the deleted range.
// hasLinesBelow: if true, cursor is not clamped when deletion includes the last line,
// because the real buffer has lines below that the cursor would land on.
void deleteLineRangeAndUpdatePos(Lines& lines, const LineRange& range, CursorPos& pos,
                                 bool hasLinesBelow = false);
void deleteOperatorLineRangeAndUpdatePos(Lines& lines, const LineRange& range, CursorPos& pos,
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
