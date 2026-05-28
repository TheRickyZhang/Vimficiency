#include "VimEditUtils.h"
#include "VimCore.h"
#include "VimOptions.h"

#include <algorithm>
#include <cassert>
#include <string_view>
#include <vector>

using namespace std;

namespace VimCore {

namespace {

struct CharDeletionBufferEffect {
  bool removedAnchorLine = false;
  bool removedAllVisibleLines = false;
};

bool realBufferHasAnotherLine(const Lines& lines, LineDeleteContext context) {
  return lines.size() > 1 || context.hasLinesAbove || context.hasLinesBelow;
}

CharDeletionBufferEffect applyCharDeletionToBuffer(
    Lines& lines, const CharRange& r, const CursorPos& originalPos,
    Mode mode, LineDeleteContext context) {
  // Whether the original cursor was already on the anchor line affects Vim's
  // empty-line retention rules when a deletion clears that line.
  bool cursorOnDeletionLine = (originalPos.line == r.begin.line);
  CharDeletionBufferEffect effect;

  assert(r.begin.line >= 0 && r.begin.line < static_cast<int>(lines.size()));
  assert(r.end.line >= r.begin.line && r.end.line < static_cast<int>(lines.size()));
  assert(r.begin.col >= 0 && r.begin.col <= static_cast<int>(lines[r.begin.line].size()));
  assert(r.end.col >= 0 && r.end.col <= static_cast<int>(lines[r.end.line].size()));

  if (r.begin.line == r.end.line) {
    string& ln = lines[r.begin.line];
    int beginCol = std::clamp(r.begin.col, 0, static_cast<int>(ln.size()));
    int endCol = std::clamp(r.end.col, beginCol, static_cast<int>(ln.size()));
    ln.erase(beginCol, endCol - beginCol);
    return effect;
  }

  // Multi-line deletion: merge first and last line, delete lines in between.
  string& firstLn = lines[r.begin.line];
  const string& endLn = lines[r.end.line];
  int beginCol = std::clamp(r.begin.col, 0, static_cast<int>(firstLn.size()));
  int endCol = std::clamp(r.end.col, 0, static_cast<int>(endLn.size()));

  firstLn = firstLn.substr(0, beginCol) + endLn.substr(endCol);
  lines.erase(lines.begin() + r.begin.line + 1, lines.begin() + r.end.line + 1);

  if (firstLn.empty() && realBufferHasAnotherLine(lines, context) &&
      mode != Mode::Insert) {
    lines.erase(lines.begin() + r.begin.line);
    effect.removedAnchorLine = true;
    effect.removedAllVisibleLines = lines.empty();
    if (lines.empty()) {
      lines.push_back("");
    }
  }

  assert(!lines.empty());
  return effect;
}

CharDeletionBufferEffect applyCharLineDeletionToBuffer(
    Lines& lines, const CharLineRange& r, Mode mode,
    LineDeleteContext context) {
  CharDeletionBufferEffect effect;
  assert(r.begin.line >= 0 && r.begin.line < static_cast<int>(lines.size()));
  assert(r.endLine > r.begin.line && r.endLine < static_cast<int>(lines.size()));
  assert(r.begin.col >= 0 && r.begin.col <= static_cast<int>(lines[r.begin.line].size()));

  string& firstLn = lines[r.begin.line];
  const string& boundaryLn = lines[r.endLine];
  int beginCol = std::clamp(r.begin.col, 0, static_cast<int>(firstLn.size()));

  firstLn = firstLn.substr(0, beginCol) + boundaryLn;
  lines.erase(lines.begin() + r.begin.line + 1, lines.begin() + r.endLine + 1);

  if (firstLn.empty() && realBufferHasAnotherLine(lines, context) &&
      mode != Mode::Insert) {
    lines.erase(lines.begin() + r.begin.line);
    effect.removedAnchorLine = true;
    effect.removedAllVisibleLines = lines.empty();
    if (lines.empty()) {
      lines.push_back("");
    }
  }

  assert(!lines.empty());
  return effect;
}

CharDeletionBufferEffect applyLineCharDeletionToBuffer(
    Lines& lines, const LineCharRange& r, const CursorPos& originalPos,
    Mode mode, LineDeleteContext context) {
  assert(r.beginLine >= 0 && r.beginLine < static_cast<int>(lines.size()));
  assert(r.end.line >= r.beginLine && r.end.line < static_cast<int>(lines.size()));
  assert(r.end.col >= 0 && r.end.col <= static_cast<int>(lines[r.end.line].size()));

  return applyCharDeletionToBuffer(
      lines, CharRange(CursorPos(r.beginLine, 0), r.end), originalPos, mode,
      context);
}

void placeCursorAfterCharDeletion(const Lines& lines, int anchorLine, int anchorCol,
                                  CursorPos& pos, Mode mode) {
  pos.line = anchorLine;
  if (pos.line >= static_cast<int>(lines.size())) {
    pos.line = static_cast<int>(lines.size()) - 1;
  }

  int newCol = anchorCol;
  if (mode == Mode::Insert) {
    newCol = min(newCol, static_cast<int>(lines[pos.line].size()));
  } else {
    newCol = lines[pos.line].empty()
        ? 0
        : min(newCol, static_cast<int>(lines[pos.line].size()) - 1);
  }
  pos.setCol(newCol);
}

int charDeletionAnchorCol(CharRange range, const CursorPos& originalPos) {
  range.normalize();
  if (range.begin.col == 0 &&
      originalPos.col > 0) {
    if (range.begin.line == originalPos.line &&
        range.end.line > range.begin.line) {
      return originalPos.col;
    }
    if (range.begin.line < originalPos.line &&
        range.end.line == originalPos.line) {
      return originalPos.col;
    }
  }
  return range.begin.col;
}

bool placeCursorOutsideAfterRemovingAllVisibleLines(
    const CharDeletionBufferEffect& effect, const Lines& lines,
    LineDeleteContext context, CursorPos& pos) {
  if (!effect.removedAllVisibleLines) return false;
  if (context.hasLinesBelow) {
    pos.line = static_cast<int>(lines.size());
    pos.setCol(0);
    return true;
  }
  if (context.hasLinesAbove) {
    pos.line = -1;
    pos.setCol(0);
    return true;
  }
  return false;
}

bool hasContent(const LineCharRange& range) {
  return range.isValid()
      && (range.beginLine < range.end.line
          || (range.beginLine == range.end.line && range.end.col > 0));
}

bool linewiseDeleteLandsOnFollowingVisibleLine(
    LineRange range, const Lines& lines) {
  range.normalize();
  return range.endLine < static_cast<int>(lines.size());
}

bool shouldUseExclusiveLinewiseFirstNonBlank(
    const Lines& lines, const ResolvedDeleteRange& resolved) {
  // Legacy Vim `'startofline'` behavior: post-exclusive-linewise the `d`
  // operator goes to first-non-blank of the resulting line. With
  // `'nostartofline'` (Neovim default), cursor preserves targetCol clamped —
  // which `deleteLineRangeAndUpdatePos` already supplies through its
  // startofline-conditional branch. Trip wire:
  // `TransformOptimizerGeneratedPropertyTest.MultiLineEmbeddedChangeTopResultsReplay`
  // (`d(` from (2, 2) over `["~d! ", " J<", " C ", " C p"]` — Vim leaves
  // cursor at (1, 2); first-non-blank override placed it at (1, 1)).
  if constexpr (!VimOptions::startOfLine()) return false;
  CursorPos begin = resolved.linewiseMotionBegin;
  return resolved.kind == ResolvedDeleteRangeKind::Linewise &&
         resolved.linewiseCursorPolicy == LinewiseDeleteCursorPolicy::ExclusiveMotion &&
         linewiseDeleteLandsOnFollowingVisibleLine(resolved.lineRange, lines) &&
         begin.line >= 0 &&
         begin.line < static_cast<int>(lines.size()) &&
         begin.col > 0 &&
         !isBlankLine(lines[begin.line]) &&
         hasOnlyBlankPrefix(lines[begin.line], begin.col);
}

void applyExclusiveLinewiseCursorPolicy(
    const Lines& lines, const ResolvedDeleteRange& resolved,
    const CursorPos& originalPos, bool adjustToFirstNonBlank, CursorPos& pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return;
  if (isBlankLine(lines[pos.line])) {
    LineRange range = resolved.lineRange;
    range.normalize();
    bool backwardExclusive = originalPos.line >= range.endLine - 1;
    if (backwardExclusive) {
      int lastCol = lines[pos.line].lastCol();
      pos.setCol(originalPos.col == 0 ? lastCol : min(originalPos.col, lastCol));
    } else if constexpr (VimOptions::startOfLine()) {
      pos.setCol(0);
    }
    // nostartofline (Neovim default): forward exclusive-linewise leaves the
    // cursor where deleteOperatorLineRangeAndUpdatePos clamped it (targetCol
    // clamped to lastCol). Trip wire: `d}` from `(1, 2)` over
    // `["  ", "   K6~ ", "zg~"]` — Vim keeps cursor at (0, 1), not (0, 0).
    return;
  }
  if (adjustToFirstNonBlank) {
    pos.setCol(firstNonBlankColInLine(lines[pos.line]));
  }
}

bool didDeleteRangeRemoveBeginLine(CharRange range,
                                   int oldLineCount,
                                   int newLineCount) {
  range.normalize();
  int baselineRemoved = range.end.line - range.begin.line;
  return oldLineCount - newLineCount > baselineRemoved;
}

void applyBackwardWordCharwiseCursorPolicy(
    CharRange range, int oldLineCount, const CursorPos& originalPos,
    const ResolvedDeleteRange& resolved, const Lines& lines, CursorPos& pos) {
  range.normalize();

  int cursorContentStart =
      originalPos.line == 0 ? resolved.firstContentCol : 0;
  if (originalPos.col != cursorContentStart ||
      originalPos.line <= range.begin.line) {
    return;
  }
  if (!didDeleteRangeRemoveBeginLine(
          range, oldLineCount, static_cast<int>(lines.size()))) {
    return;
  }
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return;
  if (lines[pos.line].empty()) return;

  if (isBlankLine(lines[pos.line])) {
    pos.setCol(lines[pos.line].lastCol());
  } else {
    pos.setCol(firstNonBlankColInLine(lines[pos.line]));
  }
}

void applyBackwardWordLinewiseCursorPolicy(
    const ResolvedDeleteRange& resolved, const CursorPos& originalPos,
    const Lines& lines, CursorPos& pos) {
  int cursorContentStart =
      originalPos.line == 0 ? resolved.firstContentCol : 0;
  if (originalPos.col != cursorContentStart) return;

  LineRange range = resolved.lineRange;
  range.normalize();
  if (range.endLine != originalPos.line) return;
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return;
  if (!isBlankLine(lines[pos.line])) return;

  pos.setCol(lines[pos.line].lastCol());
}

bool isJoinWhitespace(char c) {
  return c == ' ' || c == '\t';
}

bool endsJoinWhitespace(const string& line) {
  return !line.empty() && isJoinWhitespace(line.back());
}

size_t firstJoinContentCol(const string& line, bool addSpace) {
  if (!addSpace) return 0;
  size_t col = 0;
  while (col < line.size() && isJoinWhitespace(line[col])) {
    col++;
  }
  return col;
}

bool endsWithDashBeforeJoinWhitespace(const string& line) {
  int col = static_cast<int>(line.size()) - 1;
  bool sawWhitespace = false;
  while (col >= 0 && isJoinWhitespace(line[col])) {
    sawWhitespace = true;
    col--;
  }
  return sawWhitespace && col >= 0 && line[col] == '-';
}

size_t skipJoinCommentLeader(const string& currentLine,
                             const string& nextLine,
                             size_t start,
                             bool addSpace) {
  if (!addSpace || start >= nextLine.size() || nextLine[start] != '#') {
    return start;
  }
  if (!endsWithDashBeforeJoinWhitespace(currentLine)) return start;

  start++;
  while (start < nextLine.size() && isJoinWhitespace(nextLine[start])) {
    start++;
  }
  return start;
}

void appendJoinSpace(string& line) {
  bool needsTwoSpaces =
      VimOptions::joinSpaces() &&
      (line.back() == '.' || line.back() == '!' || line.back() == '?');
  line += needsTwoSpaces ? "  " : " ";
}

}  // namespace

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

void deleteRangeAndUpdatePos(
    Lines& lines, const CharRange& range, CursorPos& pos, Mode mode,
    LineDeleteContext context) {
  CharRange r = range;
  r.normalize();
  if (r.isEmpty()) return;
  CursorPos originalPos = pos;
  CharDeletionBufferEffect effect =
      applyCharDeletionToBuffer(lines, r, originalPos, mode, context);
  if (placeCursorOutsideAfterRemovingAllVisibleLines(
          effect, lines, context, pos)) {
    return;
  }
  int anchorCol = effect.removedAnchorLine
      ? charDeletionAnchorCol(r, originalPos)
      : r.begin.col;
  placeCursorAfterCharDeletion(lines, r.begin.line, anchorCol, pos, mode);
}

void deleteCharLineRangeAndUpdatePos(Lines& lines, const CharLineRange& range,
                                     CursorPos& pos, Mode mode,
                                     LineDeleteContext context) {
  assert(range.isValid());
  CharDeletionBufferEffect effect =
      applyCharLineDeletionToBuffer(lines, range, mode, context);
  if (placeCursorOutsideAfterRemovingAllVisibleLines(
          effect, lines, context, pos)) {
    return;
  }
  placeCursorAfterCharDeletion(lines, range.begin.line, range.begin.col, pos, mode);
}

void deleteLineCharRangeAndUpdatePos(Lines& lines, const LineCharRange& range,
                                     CursorPos& pos, Mode mode,
                                     LineDeleteContext context) {
  assert(range.isValid());
  CursorPos originalPos = pos;
  CharDeletionBufferEffect effect =
      applyLineCharDeletionToBuffer(lines, range, originalPos, mode, context);
  if (placeCursorOutsideAfterRemovingAllVisibleLines(
          effect, lines, context, pos)) {
    return;
  }
  placeCursorAfterCharDeletion(lines, range.beginLine, 0, pos, mode);
}

void deleteResolvedRangeAndUpdatePos(
    Lines& lines, const ResolvedDeleteRange& resolved, CursorPos& pos,
    Mode mode, LineDeleteContext context) {
  CursorPos originalPos = pos;
  int oldLineCount = static_cast<int>(lines.size());

  switch (resolved.kind) {
    case ResolvedDeleteRangeKind::Characterwise:
      if (!resolved.charRange.isEmpty()) {
        deleteRangeAndUpdatePos(lines, resolved.charRange, pos, mode, context);
        if (resolved.cursorPolicy ==
            DeleteCursorPolicy::BackwardWordOperatorMotion) {
          applyBackwardWordCharwiseCursorPolicy(
              resolved.charRange, oldLineCount, originalPos, resolved,
              lines, pos);
        }
      } else if (mode == Mode::Insert) {
        pos = resolved.charRange.begin;
      }
      return;
    case ResolvedDeleteRangeKind::CharLine:
      deleteCharLineRangeAndUpdatePos(
          lines, resolved.charLineRange, pos, mode, context);
      return;
    case ResolvedDeleteRangeKind::LineChar:
      if (hasContent(resolved.lineCharRange)) {
        deleteLineCharRangeAndUpdatePos(
            lines, resolved.lineCharRange, pos, mode, context);
      }
      return;
    case ResolvedDeleteRangeKind::Linewise:
      {
        bool adjustToFirstNonBlank =
            shouldUseExclusiveLinewiseFirstNonBlank(lines, resolved);
        // TRUE exclusive-linewise (`:help exclusive-linewise`) for FORWARD
        // motions where the delete leaves lines AFTER the deleted range:
        // the post-delete cursor lands on the shifted-in line, and Vim's
        // motion sets curswant to the endpoint's column before the operator
        // runs. We collapse motion+operator into one apply, so override
        // pos.targetCol here. When the delete CONSUMES the buffer tail, the
        // cursor falls back to the line ABOVE the deleted range and Vim
        // preserves the original cursor's curswant — skip the override.
        if (resolved.linewiseCursorPolicy ==
                LinewiseDeleteCursorPolicy::ExclusiveMotion &&
            resolved.exclusiveMotionEndCol >= 0) {
          LineRange r = resolved.lineRange;
          r.normalize();
          bool backwardExclusive = originalPos.line >= r.endLine - 1;
          bool deleteConsumesTail =
              r.endLine >= static_cast<int>(lines.size());
          if (!backwardExclusive && !deleteConsumesTail) {
            pos.targetCol = resolved.exclusiveMotionEndCol;
          }
        }
        if (resolved.linewiseCursorPolicy ==
            LinewiseDeleteCursorPolicy::LinewiseCommand) {
          deleteLineRangeAndUpdatePos(lines, resolved.lineRange, pos, context);
        } else {
          deleteOperatorLineRangeAndUpdatePos(
              lines, resolved.lineRange, pos, context);
        }
        if (resolved.linewiseCursorPolicy ==
            LinewiseDeleteCursorPolicy::ExclusiveMotion) {
          applyExclusiveLinewiseCursorPolicy(
              lines, resolved, originalPos, adjustToFirstNonBlank, pos);
        }
        if (resolved.cursorPolicy ==
            DeleteCursorPolicy::BackwardWordOperatorMotion) {
          applyBackwardWordLinewiseCursorPolicy(
              resolved, originalPos, lines, pos);
        }
        return;
      }
  }
}

void deleteLineRangeAndUpdatePos(Lines& lines, const LineRange& range, CursorPos& pos,
                                 LineDeleteContext context) {
  LineRange r = range;
  r.normalize();

  assert(r.beginLine >= 0 && r.beginLine < static_cast<int>(lines.size()));
  assert(r.endLine > r.beginLine && r.endLine <= static_cast<int>(lines.size()));

  int oldSize = static_cast<int>(lines.size());
  bool deletedAllVisibleLines = r.beginLine == 0 && r.endLine == oldSize;

  lines.erase(lines.begin() + r.beginLine, lines.begin() + r.endLine);

  // Maintain invariant: buffer always has at least one line
  if (lines.empty()) {
    lines.push_back("");
  }

  int newSize = static_cast<int>(lines.size());
  if (deletedAllVisibleLines) {
    if (context.hasLinesBelow) {
      pos.line = newSize;
      return;
    }
    if (context.hasLinesAbove) {
      pos.line = -1;
      return;
    }
  }

  // If deletion removed the last lines and there are lines below in the real
  // buffer, cursor goes to the line below (past the end of effective lines).
  // Column is left unset — caller must apply 'k' to bring it back in range.
  if (context.hasLinesBelow && r.beginLine >= newSize) {
    pos.line = r.beginLine;  // Past end of effective lines
    return;
  }

  pos.line = min(r.beginLine, newSize - 1);
  if constexpr (VimOptions::startOfLine()) {
    // Legacy Vim: dd goes to first non-blank of the new current line
    pos.setCol(firstNonBlankColInLine(lines[pos.line]));
  } else {
    // Neovim: dd resets targetCol to the clamped column
    if (lines[pos.line].empty()) {
      pos.setCol(0);
    } else {
      pos.setCol(min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
    }
  }
}

void deleteOperatorLineRangeAndUpdatePos(Lines& lines, const LineRange& range,
                                         CursorPos& pos, LineDeleteContext context) {
  LineRange r = range;
  r.normalize();
  int originalLine = pos.line;
  // Special case: `dw` from a truly empty line crosses to the next line.
  // The motion endpoint is at col 0 of the next line, producing a linewise
  // range. Vim places the cursor at first-non-blank of the remaining line —
  // unlike the normal forward-exclusive-linewise cursor preservation that
  // applies when the original line had whitespace content.
  bool originalLineWasEmpty =
      originalLine >= 0 && originalLine < static_cast<int>(lines.size()) &&
      lines[originalLine].empty();
  deleteLineRangeAndUpdatePos(lines, range, pos, context);
  if (r.endLine > originalLine && !originalLineWasEmpty) {
    return;
  }
  if (!context.hasLinesBelow && r.beginLine >= static_cast<int>(lines.size())) {
    return;
  }
  if (pos.line >= 0 && pos.line < static_cast<int>(lines.size())) {
    pos.setCol(firstNonBlankColInLine(lines[pos.line]));
  }
}

void insertText(Lines& lines, CursorPos& pos, string_view text) {
  if (text.empty()) return;

  assert(!lines.empty() && "Lines invariant: buffer always has at least one line");
  assert(pos.line >= 0 && pos.line < static_cast<int>(lines.size()));
  assert(pos.col >= 0 && pos.col <= static_cast<int>(lines[pos.line].size()));

  // Split the text into lines
  vector<string> textLines;
  string current;
  for (char c : text) {
    if (c == '\n') {
      textLines.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  textLines.push_back(current);

  if (textLines.size() == 1) {
    // Simple single-line insert
    lines[pos.line].insert(pos.col, text);
    pos.setCol(pos.col + static_cast<int>(text.size()));
  } else {
    // Multi-line insert
    string& originalLine = lines[pos.line];
    string before = originalLine.substr(0, pos.col);
    string after = originalLine.substr(pos.col);

    // Modify the current line to be: before + first inserted text
    originalLine = before + textLines[0];

    // Insert middle lines and last line
    int insertPos = pos.line + 1;
    for (size_t i = 1; i < textLines.size(); i++) {
      if (i == textLines.size() - 1) {
        // Last line: insert text + after
        lines.insert(lines.begin() + insertPos, textLines[i] + after);
        pos = CursorPos(insertPos, static_cast<int>(textLines[i].size()));
      } else {
        lines.insert(lines.begin() + insertPos, textLines[i]);
      }
      insertPos++;
    }
  }
}

namespace {

// Port of Neovim's `do_join` (src/nvim/ops.c). Joins `count` consecutive lines
// starting at pos.line into a single line, updating pos.col per Vim's rule:
//
//   col = sumsize - currsize - spaces[count - 1]
//
// where sumsize is the total joined length, currsize is the length of the
// last joined segment (after skipwhite for J), and spaces[i] is the number
// of spaces inserted before segment i (1 for normal J between non-empty
// content, +1 more under joinspaces+sentence-end, 0 elsewhere).
//
// **Comment-leader stripping is intentionally NOT modeled.** Real Vim's
// `'comments'` option (default: `s1:/*,mb:*,ex:*/,b:#,fb:-,:%,:#,...`) causes
// J/gJ to strip recognized leaders (`#`, `%`, `>`, `*`, etc.) and surrounding
// whitespace from the next line. We don't reproduce that here because the
// full format is flag-rich and porting it faithfully is significant work for
// limited optimizer benefit. To keep our model and the oracle consistent,
// NeovimOracle sets `comments=` (empty). Trade-off: optimizer output may
// differ from real Vim when the user's buffer joins onto a comment-leader
// line. See dev/decisions.md (J comment-leader stripping).
void doJoin(Lines& lines, CursorPos& pos, int count, bool insertSpace) {
  assert(count >= 2);
  assert(pos.line + count <= static_cast<int>(lines.size()));

  std::vector<int> spaces(count, 0);
  std::vector<string_view> segments(count);
  int currsize = 0;
  int sumsize = 0;
  char endcurr1 = '\0';
  char endcurr2 = '\0';

  for (int t = 0; t < count; t++) {
    string_view curr = lines[pos.line + t];

    // Skip leading whitespace on continuation segments for J (not gJ).
    if (insertSpace && t > 0) {
      while (!curr.empty() && isJoinWhitespace(curr.front())) {
        curr.remove_prefix(1);
      }
    }

    // Decide whether to insert a separator space before this segment.
    if (insertSpace && t > 0) {
      bool nonEmpty = !curr.empty();
      bool notCloseParen = nonEmpty && curr.front() != ')';
      bool prevNotTab = endcurr1 != '\t';
      if (nonEmpty && notCloseParen && sumsize != 0 && prevNotTab) {
        if (endcurr1 == ' ') {
          // Previous segment already ends in a space; don't add another, but
          // for sentence-end double-spacing we look one step further back.
          endcurr1 = endcurr2;
        } else {
          spaces[t]++;
        }
        if (VimOptions::joinSpaces() &&
            (endcurr1 == '.' || endcurr1 == '!' || endcurr1 == '?')) {
          spaces[t]++;
        }
      }
    }

    segments[t] = curr;
    currsize = static_cast<int>(curr.size());
    sumsize += currsize + spaces[t];

    endcurr1 = endcurr2 = '\0';
    if (insertSpace && currsize > 0) {
      endcurr1 = curr.back();
      if (currsize > 1) endcurr2 = curr[currsize - 2];
    }
  }

  // Build the joined line.
  string joined;
  joined.reserve(static_cast<size_t>(sumsize));
  for (int t = 0; t < count; t++) {
    joined.append(static_cast<size_t>(spaces[t]), ' ');
    joined.append(segments[t]);
  }

  int col = sumsize - currsize - spaces[count - 1];

  lines[pos.line] = std::move(joined);
  lines.erase(
      lines.begin() + pos.line + 1,
      lines.begin() + pos.line + count);

  const string& result = lines[pos.line];
  int lastCol = result.empty() ? 0 : static_cast<int>(result.size()) - 1;
  pos.setCol(std::min(col, lastCol));
}

}  // namespace

void joinLines(Lines& lines, CursorPos& pos, bool addSpace) {
  doJoin(lines, pos, 2, addSpace);
}

void joinLineRange(Lines& lines, CursorPos& pos, int lineCount, bool addSpace) {
  doJoin(lines, pos, lineCount, addSpace);
}

void openLineBelow(Lines& lines, CursorPos& pos) {
  assert(!lines.empty());
  assert(pos.line >= 0 && pos.line < static_cast<int>(lines.size()));

  lines.insert(lines.begin() + pos.line + 1, "");
  pos = CursorPos(pos.line + 1, 0);
}

void openLineAbove(Lines& lines, CursorPos& pos) {
  assert(!lines.empty());
  assert(pos.line >= 0 && pos.line < static_cast<int>(lines.size()));

  lines.insert(lines.begin() + pos.line, "");
  pos.setCol(0);
  // pos.line stays the same (now points to the new empty line)
}

} // namespace VimCore
