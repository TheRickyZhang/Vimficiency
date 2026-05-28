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
#include "VimCore/VimEndpointUtils.h"
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
//     Exception:  if that kept prefix is only whitespace, Vim removes the
//                 line break too and the operation becomes linewise.
//
// Callers:
//   - EditInterpreter.cpp:  d)/c), d(/c(, d}/c}, d{/c{
//   - TransformExplorer.cpp:     exploreSentenceEdits (both directions)
//
// Operator-specific notes:
//   - d with Linewise: use deleteLineRangeAndUpdatePos() for proper cursor placement.
//   - c with Linewise: EditInterpreter handles the autoindented replacement
//     line before using this resolver for backed-up characterwise cases.
//   - d} EOF callers extend the inclusive last-char endpoint to a half-open
//     range before this resolver decides characterwise vs linewise shape.
//
// Oracle-verified tests: ExclusiveLineAdjust_* and ChangeMotionsTest.
//

enum class ResolvedDeleteRangeKind {
  Characterwise,
  CharLine,
  LineChar,
  Linewise,
};

enum class LinewiseDeleteCursorPolicy {
  LinewiseCommand,
  OperatorMotion,
  ExclusiveMotion,
};

enum class DeleteCursorPolicy {
  Default,
  BackwardWordOperatorMotion,
};

struct ResolvedDeleteRange {
  ResolvedDeleteRangeKind kind;
  CharRange charRange;
  CharLineRange charLineRange;
  LineCharRange lineCharRange;
  LineRange lineRange;
  LinewiseDeleteCursorPolicy linewiseCursorPolicy;
  CursorPos linewiseMotionBegin;
  DeleteCursorPolicy cursorPolicy;
  int firstContentCol;
  // For TRUE exclusive-linewise (`:help exclusive-linewise`) promotions only:
  // the column of the motion endpoint. Vim's `)`/`}` etc. update curswant to
  // this column BEFORE the linewise delete; the post-delete cursor's
  // targetCol must reflect that. -1 when not applicable (result-is-blank
  // promotions keep the original cursor's targetCol).
  int exclusiveMotionEndCol;

  static ResolvedDeleteRange characterwise(
      CharRange range,
      DeleteCursorPolicy cursorPolicy = DeleteCursorPolicy::Default,
      int firstContentCol = 0) {
    return {ResolvedDeleteRangeKind::Characterwise, range,
            CHAR_LINE_RANGE_OUTSIDE_BOUNDARY,
            LINE_CHAR_RANGE_OUTSIDE_BOUNDARY, LineRange(0, 0),
            LinewiseDeleteCursorPolicy::LinewiseCommand,
            CursorPos(-1, -1), cursorPolicy, firstContentCol, -1};
  }

  static ResolvedDeleteRange charLine(CharLineRange range) {
    return {ResolvedDeleteRangeKind::CharLine, CharRange(),
            range, LINE_CHAR_RANGE_OUTSIDE_BOUNDARY, LineRange(0, 0),
            LinewiseDeleteCursorPolicy::LinewiseCommand,
            CursorPos(-1, -1), DeleteCursorPolicy::Default, 0, -1};
  }

  static ResolvedDeleteRange lineChar(LineCharRange range) {
    return {ResolvedDeleteRangeKind::LineChar, CharRange(),
            CHAR_LINE_RANGE_OUTSIDE_BOUNDARY, range, LineRange(0, 0),
            LinewiseDeleteCursorPolicy::LinewiseCommand,
            CursorPos(-1, -1), DeleteCursorPolicy::Default, 0, -1};
  }

  static ResolvedDeleteRange linewise(
      LineRange range, LinewiseDeleteCursorPolicy cursorPolicy,
      CursorPos motionBegin = CursorPos(-1, -1),
      DeleteCursorPolicy deleteCursorPolicy = DeleteCursorPolicy::Default,
      int firstContentCol = 0,
      int exclusiveMotionEndCol = -1) {
    return {ResolvedDeleteRangeKind::Linewise, CharRange(),
            CHAR_LINE_RANGE_OUTSIDE_BOUNDARY,
            LINE_CHAR_RANGE_OUTSIDE_BOUNDARY, range,
            cursorPolicy, motionBegin, deleteCursorPolicy, firstContentCol,
            exclusiveMotionEndCol};
  }
};

// Resolve a raw exclusive [begin, end) range into an explicit characterwise
// CharRange or linewise LineRange. For change-like operators, handle their
// linewise replacement case before calling this with `allowLinewise=false`.
inline bool hasOnlyBlankPrefix(std::string_view line, int endCol, int contentStartCol = 0) {
  int begin = std::clamp(contentStartCol, 0, static_cast<int>(line.size()));
  int end = std::clamp(endCol, begin, static_cast<int>(line.size()));
  for (int col = begin; col < end; col++) {
    char c = line[col];
    if (c != ' ' && c != '\t') return false;
  }
  return true;
}

inline bool hasOnlyBlankSuffix(std::string_view line, int beginCol) {
  int begin = std::clamp(beginCol, 0, static_cast<int>(line.size()));
  for (int col = begin; col < static_cast<int>(line.size()); col++) {
    char c = line[col];
    if (c != ' ' && c != '\t') return false;
  }
  return true;
}

// Returns 1 if TRUE exclusive-linewise (`:help exclusive-linewise`),
// 2 if op_delete result-is-blank promotion (Vim's ops.c:741-757), 0 otherwise.
// Distinguishes the two because their post-delete cursor targetCol semantics
// differ: exclusive-linewise uses the motion endpoint's curswant; result-is-
// blank keeps the original cursor's curswant.
inline int classifyExclusiveDeleteLinewisePromotion(
    CharRange range, const Lines& lines) {
  range.normalize();
  bool spansForwardLines = range.begin.line < range.end.line;
  bool endAtLineStart = range.end.col == 0;
  bool consumesBufferTail = !endAtLineStart &&
      range.end.line == lines.lastLine() &&
      range.end.col >= static_cast<int>(lines[range.end.line].size());

  if (spansForwardLines &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col) &&
      (endAtLineStart || consumesBufferTail)) {
    return 1;
  }
  if (spansForwardLines &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col) &&
      hasOnlyBlankSuffix(lines[range.end.line], range.end.col)) {
    return 2;
  }
  return 0;
}

inline bool exclusiveDeletePromotesToLinewise(CharRange range, const Lines& lines) {
  range.normalize();
  bool spansForwardLines = range.begin.line < range.end.line;
  bool endAtLineStart = range.end.col == 0;
  bool consumesBufferTail = !endAtLineStart &&
      range.end.line == lines.lastLine() &&
      range.end.col >= static_cast<int>(lines[range.end.line].size());

  if (spansForwardLines &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col) &&
      (endAtLineStart || consumesBufferTail)) {
    return true;
  }

  // Vim's op_delete (ops.c:741-757) extra promotion rule: when a char-wise
  // delete spans multi-line and the result would be a blank line, promote.
  // Result-is-blank requires:
  //   - cols [0, begin.col) on begin's line are all whitespace, AND
  //   - chars after end.col on end's line are all whitespace.
  // Exclusive motion's end is past the last char to delete, so we check
  // hasOnlyBlankSuffix from end.col (no +inclusive shift since inclusive
  // is false for exclusive motions).
  if (spansForwardLines &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col) &&
      hasOnlyBlankSuffix(lines[range.end.line], range.end.col)) {
    return true;
  }

  return false;
}

inline LineRange exclusiveMotionLinewiseRange(CharRange range) {
  range.normalize();
  int endLine = range.end.col == 0 ? range.end.line : range.end.line + 1;
  return LineRange(range.begin.line, endLine);
}

inline bool exclusiveChangePromotesToLinewise(CharRange range, const Lines& lines) {
  range.normalize();
  return range.begin.line < range.end.line &&
         range.end.col == 0 &&
         (range.begin.col == 0 || isBlankLine(lines[range.begin.line]));
}

inline ResolvedDeleteRange resolveExclusiveDeleteRange(
    CharRange range, const Lines& lines, bool allowLinewise) {
  range.normalize();

  if (allowLinewise) {
    int classification = classifyExclusiveDeleteLinewisePromotion(range, lines);
    if (classification == 1) {
      // TRUE exclusive-linewise: motion endpoint curswant feeds post-delete
      // cursor targetCol.
      return ResolvedDeleteRange::linewise(
          exclusiveMotionLinewiseRange(range),
          LinewiseDeleteCursorPolicy::ExclusiveMotion,
          range.begin,
          DeleteCursorPolicy::Default,
          /*firstContentCol=*/0,
          /*exclusiveMotionEndCol=*/range.end.col);
    }
    if (classification == 2) {
      return ResolvedDeleteRange::linewise(
          exclusiveMotionLinewiseRange(range),
          LinewiseDeleteCursorPolicy::ExclusiveMotion,
          range.begin);
    }
  }

  if (range.end.col != 0 || range.end.line <= range.begin.line) {
    if (allowLinewise && range.begin.col == 0 &&
        range.begin.line < range.end.line &&
        hasOnlyBlankSuffix(lines[range.end.line], range.end.col)) {
      return ResolvedDeleteRange::linewise(
          LineRange(range.begin.line, range.end.line + 1),
          LinewiseDeleteCursorPolicy::ExclusiveMotion,
          range.begin);
    }
    if (range.begin.col == 0 && range.begin.line < range.end.line) {
      return ResolvedDeleteRange::lineChar(LineCharRange(range.begin.line, range.end));
    }
    return ResolvedDeleteRange::characterwise(range);
  }

  if (allowLinewise &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col)) {
    return ResolvedDeleteRange::linewise(
        LineRange(range.begin.line, range.end.line),
        LinewiseDeleteCursorPolicy::ExclusiveMotion,
        range.begin);
  }

  if (!allowLinewise &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col)) {
    range.begin.col = firstNonBlankColInLine(lines[range.begin.line]);
  }

  if (range.begin.col == 0) {
    return ResolvedDeleteRange::characterwise(
        CharRange(range.begin,
                  CursorPos(range.end.line - 1,
                            static_cast<int>(lines[range.end.line - 1].size()))));
  }

  range.end = CursorPos(range.end.line - 1,
                        static_cast<int>(lines[range.end.line - 1].size()));
  return ResolvedDeleteRange::characterwise(range);
}

inline ResolvedDeleteRange resolveExclusiveChangeRange(CharRange range, const Lines& lines) {
  range.normalize();
  if (exclusiveChangePromotesToLinewise(range, lines)) {
    return ResolvedDeleteRange::linewise(
        exclusiveMotionLinewiseRange(range),
        LinewiseDeleteCursorPolicy::ExclusiveMotion,
        range.begin);
  }
  return resolveExclusiveDeleteRange(range, lines, false);
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

inline ResolvedDeleteRange resolveBackwardExclusiveWordDeleteRange(
    const CursorPos& endpoint, const CursorPos& cursor, const Lines& lines,
    int contentStartCol = 0) {
  if (endpoint.line < cursor.line && cursor.col != contentStartCol &&
      VimCore::isBlankLine(lines[cursor.line]) &&
      hasOnlyBlankPrefix(
          lines[endpoint.line], endpoint.col,
          endpoint.line == 0 ? contentStartCol : 0)) {
    return ResolvedDeleteRange::linewise(
        LineRange(endpoint.line, cursor.line + 1),
        LinewiseDeleteCursorPolicy::OperatorMotion,
        endpoint,
        DeleteCursorPolicy::BackwardWordOperatorMotion,
        contentStartCol);
  }

  if (cursor.col == contentStartCol && endpoint.line < cursor.line) {
    int endpointContentStart = endpoint.line == 0 ? contentStartCol : 0;
    if (endpointContentStart == 0 &&
        hasOnlyBlankPrefix(lines[endpoint.line], endpoint.col, endpointContentStart)) {
      return ResolvedDeleteRange::linewise(
          LineRange(endpoint.line, cursor.line),
          LinewiseDeleteCursorPolicy::OperatorMotion,
          endpoint,
          DeleteCursorPolicy::BackwardWordOperatorMotion,
          contentStartCol);
    }
  }

  return ResolvedDeleteRange::characterwise(
      buildBackwardExclusiveCharRange(endpoint, cursor, lines, contentStartCol),
      DeleteCursorPolicy::BackwardWordOperatorMotion,
      contentStartCol);
}

inline ResolvedDeleteRange resolveForwardInclusiveWordEndDeleteRange(
    CharRange range, const Lines& lines, int contentStartCol = 0) {
  range.normalize();
  if (range.begin.line < range.end.line &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col,
                         range.begin.line == 0 ? contentStartCol : 0) &&
      hasOnlyBlankSuffix(lines[range.end.line], range.end.col)) {
    // op_delete result-is-blank promotion. Cursor honors `'nostartofline'` —
    // see resolveBackwardInclusiveWordEndDeleteRange and dev/decisions.md.
    return ResolvedDeleteRange::linewise(
        LineRange(range.begin.line, range.end.line + 1),
        LinewiseDeleteCursorPolicy::LinewiseCommand,
        range.begin);
  }

  return ResolvedDeleteRange::characterwise(range);
}

inline ResolvedDeleteRange resolveBackwardInclusiveWordEndDeleteRange(
    const CursorPos& endpoint, const CursorPos& cursor, const Lines& lines) {
  // Multi-line inclusive delete where the JOIN-RESULT is a blank line:
  // prefix on begin's line (cols [0, begin.col)) and suffix on end's line
  // (cols [end.col, EOL)) are all whitespace. Vim's op_delete result-is-blank
  // promotion (ops.c:741-757). Vim's `beginline(BL_WHITE)` here is overridden
  // by `'nostartofline'` (Neovim default) for the `d` operator — cursor keeps
  // the same column rather than jumping to first non-blank. Use the
  // LinewiseCommand cursor policy (which preserves targetCol) to match.
  CharRange range(endpoint, VimCore::onePastOnSameLine(lines, cursor));
  if (range.begin.line < range.end.line &&
      hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col) &&
      hasOnlyBlankSuffix(lines[range.end.line], range.end.col)) {
    return ResolvedDeleteRange::linewise(
        LineRange(range.begin.line, range.end.line + 1),
        LinewiseDeleteCursorPolicy::LinewiseCommand,
        range.begin);
  }

  return ResolvedDeleteRange::characterwise(range);
}

// Single entry point for resolving a word-operator + motion delete
// (dw/dW/de/dE/db/dB/dge/dgE). Internally applies the Vim quirks that vary by
// motion shape — forward vs backward, inclusive vs exclusive — and returns a
// `ResolvedDeleteRange`. Callers should not branch on `WordOperatorTarget`
// themselves; new Vim quirks for word motions go inside this function (or
// inside the per-target helpers it dispatches to), not as new resolvers.
//
// `leftColOffset` is the per-buffer prefix length: 0 for a real buffer or for
// an embedded region whose line 0 starts at col 0; otherwise the number of
// columns of protected prefix on line 0. The dispatcher derives the relevant
// line's content-start col from `leftColOffset` per the standard convention
// (`line == 0 ? leftColOffset : 0`).
inline ResolvedDeleteRange resolveWordOperatorMotion(
    WordOperatorTarget target,
    const CharRange& motionRange,
    const CursorPos& cursor,
    const Lines& lines,
    int leftColOffset = 0) {
  auto contentStartFor = [leftColOffset](int line) {
    return line == 0 ? leftColOffset : 0;
  };
  switch (target) {
    case WordOperatorTarget::DeleteToWordEnd:
      // begin == cursor for forward motions.
      return resolveForwardInclusiveWordEndDeleteRange(
          motionRange, lines, contentStartFor(motionRange.begin.line));
    case WordOperatorTarget::DeleteBackToWordBegin:
      // Cursor.line's content start drives the cursor.col threshold.
      return resolveBackwardExclusiveWordDeleteRange(
          motionRange.begin, cursor, lines, contentStartFor(cursor.line));
    case WordOperatorTarget::DeleteBackToWordEnd:
      return resolveBackwardInclusiveWordEndDeleteRange(
          motionRange.begin, cursor, lines);
    case WordOperatorTarget::DeleteToNextWord:
      if (motionRange.end.line > cursor.line && !lines[cursor.line].empty()) {
        return ResolvedDeleteRange::characterwise(
            CharRange(cursor,
                      CursorPos(cursor.line,
                                static_cast<int>(lines[cursor.line].size()))));
      }
      return ResolvedDeleteRange::characterwise(motionRange);
  }
  return ResolvedDeleteRange::characterwise(motionRange);
}

inline ResolvedDeleteRange resolveWordOperatorChangeMotion(
    WordOperatorTarget target,
    const CharRange& motionRange,
    const CursorPos& cursor,
    const Lines& lines,
    int leftColOffset = 0) {
  auto contentStartFor = [leftColOffset](int line) {
    return line == 0 ? leftColOffset : 0;
  };
  switch (target) {
    case WordOperatorTarget::DeleteToWordEnd:
      return ResolvedDeleteRange::characterwise(motionRange);
    case WordOperatorTarget::DeleteBackToWordBegin:
      return ResolvedDeleteRange::characterwise(
          buildBackwardExclusiveCharRange(
              motionRange.begin, cursor, lines, contentStartFor(cursor.line)));
    case WordOperatorTarget::DeleteBackToWordEnd:
      return ResolvedDeleteRange::characterwise(
          CharRange(motionRange.begin, VimCore::onePastOnSameLine(lines, cursor)));
    case WordOperatorTarget::DeleteToNextWord:
      return resolveWordOperatorMotion(
          target, motionRange, cursor, lines, leftColOffset);
  }
  return ResolvedDeleteRange::characterwise(motionRange);
}

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

struct LineDeleteContext {
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
};

// Delete text in character range. Modifies lines and updates pos.
// CharRange is half-open [begin, end).
// Mode determines position clamping: Normal clamps to last char, Insert allows after last char
// Empty line removal may depend on whether the original cursor was already on
// the range's anchor line, since this API combines deletion with cursor updates.
void deleteRangeAndUpdatePos(
    Lines& lines, const CharRange& range, CursorPos& pos,
    Mode mode = Mode::Normal, LineDeleteContext context = {});
void deleteCharLineRangeAndUpdatePos(
    Lines& lines, const CharLineRange& range, CursorPos& pos,
    Mode mode = Mode::Normal, LineDeleteContext context = {});
void deleteLineCharRangeAndUpdatePos(
    Lines& lines, const LineCharRange& range, CursorPos& pos,
    Mode mode = Mode::Normal, LineDeleteContext context = {});
void deleteResolvedRangeAndUpdatePos(
    Lines& lines, const ResolvedDeleteRange& resolved, CursorPos& pos,
    Mode mode = Mode::Normal, LineDeleteContext context = {});

// Delete entire lines in [beginLine, endLine). Modifies lines and updates pos.
// Pos goes to first non-blank of the line following the deleted range.
// Hidden context is represented by out-of-range cursor lines when deleting all
// remaining visible lines: -1 for hidden lines above, size() for hidden below.
void deleteLineRangeAndUpdatePos(Lines& lines, const LineRange& range, CursorPos& pos,
                                 LineDeleteContext context = {});
void deleteOperatorLineRangeAndUpdatePos(Lines& lines, const LineRange& range, CursorPos& pos,
                                         LineDeleteContext context = {});

// Insert text at position. Handles newlines (splits into multiple lines).
// After insert, pos is at end of inserted text.
void insertText(Lines& lines, CursorPos& pos, std::string_view text);

// J/gJ - join current line with next
// addSpace=true: strip next-line indent, preserving existing trailing whitespace
// addSpace=false: simple concatenation (gJ)
void joinLines(Lines& lines, CursorPos& pos, bool addSpace);
void joinLineRange(Lines& lines, CursorPos& pos, int lineCount, bool addSpace);

// o - open new line below current, pos moves to new line col 0
// Precondition: lines not empty
void openLineBelow(Lines& lines, CursorPos& pos);

// O - open new line above current, pos moves to new line col 0
// Precondition: lines not empty
void openLineAbove(Lines& lines, CursorPos& pos);

} // namespace VimCore
