#include "VimEditUtils.h"
#include "VimCore.h"
#include "VimOptions.h"

#include <algorithm>
#include <cassert>

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

    if (ln.empty() && beginCol == 0 &&
        realBufferHasAnotherLine(lines, context) && !cursorOnDeletionLine
        && mode != Mode::Insert) {
      lines.erase(lines.begin() + r.begin.line);
      effect.removedAnchorLine = true;
      effect.removedAllVisibleLines = lines.empty();
      if (lines.empty()) {
        lines.push_back("");
      }
    }
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
  if (range.begin.line == originalPos.line &&
      range.end.line > range.begin.line &&
      range.begin.col == 0 &&
      originalPos.col > 0) {
    return originalPos.col;
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
    } else {
      pos.setCol(0);
    }
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
  while (col >= 0 && isJoinWhitespace(line[col])) {
    col--;
  }
  return col >= 0 && line[col] == '-';
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
  deleteLineRangeAndUpdatePos(lines, range, pos, context);
  if (r.endLine > originalLine) {
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

void joinLines(Lines& lines, CursorPos& pos, bool addSpace) {
  assert (pos.line+1 < lines.size());

  string& currentLine = lines[pos.line];
  // Vim places cursor at original first line length (where join occurred)
  int originalLen = static_cast<int>(currentLine.size());

  // Get next line
  string nextLine = lines[pos.line + 1];
  size_t start = firstJoinContentCol(nextLine, addSpace);
  start = skipJoinCommentLeader(currentLine, nextLine, start, addSpace);

  if (addSpace && !currentLine.empty() && start < nextLine.size() &&
      nextLine[start] != ')') {
    if (!endsJoinWhitespace(currentLine)) {
      appendJoinSpace(currentLine);
    }
  }
  currentLine += nextLine.substr(start);

  // Remove the next line
  lines.erase(lines.begin() + pos.line + 1);

  // Both J and gJ: cursor at original first line length (position where join occurred).
  // Clamp to last valid normal-mode column (join with empty next line leaves cursor at end).
  int lastCol = currentLine.empty() ? 0 : static_cast<int>(currentLine.size()) - 1;
  pos.setCol(min(originalLen, lastCol));
}

void joinLineRange(Lines& lines, CursorPos& pos, int lineCount, bool addSpace) {
  assert(lineCount >= 2);
  assert(pos.line + lineCount <= static_cast<int>(lines.size()));

  string joinedLine = lines[pos.line];
  int cursorCol = static_cast<int>(joinedLine.size());
  bool trailingWhitespaceFromContent = endsJoinWhitespace(joinedLine);

  for (int line = pos.line + 1; line < pos.line + lineCount; line++) {
    const string& nextLine = lines[line];
    size_t start = firstJoinContentCol(nextLine, addSpace);
    start = skipJoinCommentLeader(joinedLine, nextLine, start, addSpace);
    bool nextHasContent = start < nextLine.size();

    if (!addSpace) {
      cursorCol = static_cast<int>(joinedLine.size());
    } else if (!joinedLine.empty()) {
      if (!nextHasContent) {
        if (endsJoinWhitespace(joinedLine)) {
          cursorCol = static_cast<int>(joinedLine.size());
          joinedLine += ' ';
          trailingWhitespaceFromContent = false;
        }
      } else if (nextLine[start] != ')' && !endsJoinWhitespace(joinedLine)) {
        cursorCol = static_cast<int>(joinedLine.size());
        appendJoinSpace(joinedLine);
      } else if (trailingWhitespaceFromContent) {
        cursorCol = static_cast<int>(joinedLine.size());
      }
    }
    joinedLine += nextLine.substr(start);
    if (nextHasContent) {
      trailingWhitespaceFromContent = endsJoinWhitespace(nextLine);
    }
  }

  lines[pos.line] = joinedLine;
  lines.erase(
      lines.begin() + pos.line + 1,
      lines.begin() + pos.line + lineCount);

  int lastCol = joinedLine.empty()
      ? 0
      : static_cast<int>(joinedLine.size()) - 1;
  pos.setCol(min(cursorCol, lastCol));
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
