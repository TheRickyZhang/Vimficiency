#include "VimEditUtils.h"
#include "VimCore.h"
#include "VimOptions.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace VimCore {

namespace {

void applyCharDeletionToBuffer(Lines& lines, const CharRange& r,
                               const CursorPos& originalPos, Mode mode) {
  // Whether the original cursor was already on the anchor line affects Vim's
  // empty-line retention rules when a deletion clears that line.
  bool cursorOnDeletionLine = (originalPos.line == r.begin.line);

  assert(r.begin.line >= 0 && r.begin.line < static_cast<int>(lines.size()));
  assert(r.end.line >= r.begin.line && r.end.line < static_cast<int>(lines.size()));
  assert(r.begin.col >= 0 && r.begin.col <= static_cast<int>(lines[r.begin.line].size()));
  assert(r.end.col >= 0 && r.end.col <= static_cast<int>(lines[r.end.line].size()));

  if (r.begin.line == r.end.line) {
    string& ln = lines[r.begin.line];
    int beginCol = std::clamp(r.begin.col, 0, static_cast<int>(ln.size()));
    int endCol = std::clamp(r.end.col, beginCol, static_cast<int>(ln.size()));
    ln.erase(beginCol, endCol - beginCol);

    if (ln.empty() && beginCol == 0 && lines.size() > 1 && !cursorOnDeletionLine
        && mode != Mode::Insert) {
      lines.erase(lines.begin() + r.begin.line);
    }
    return;
  }

  // Multi-line deletion: merge first and last line, delete lines in between.
  string& firstLn = lines[r.begin.line];
  const string& endLn = lines[r.end.line];
  int beginCol = std::clamp(r.begin.col, 0, static_cast<int>(firstLn.size()));
  int endCol = std::clamp(r.end.col, 0, static_cast<int>(endLn.size()));

  firstLn = firstLn.substr(0, beginCol) + endLn.substr(endCol);
  lines.erase(lines.begin() + r.begin.line + 1, lines.begin() + r.end.line + 1);

  if (firstLn.empty() && lines.size() > 1 && mode != Mode::Insert) {
    lines.erase(lines.begin() + r.begin.line);
  }

  assert(!lines.empty());
}

void applyCharLineDeletionToBuffer(Lines& lines, const CharLineRange& r, Mode mode) {
  assert(r.begin.line >= 0 && r.begin.line < static_cast<int>(lines.size()));
  assert(r.endLine > r.begin.line && r.endLine < static_cast<int>(lines.size()));
  assert(r.begin.col >= 0 && r.begin.col <= static_cast<int>(lines[r.begin.line].size()));

  string& firstLn = lines[r.begin.line];
  const string& boundaryLn = lines[r.endLine];
  int beginCol = std::clamp(r.begin.col, 0, static_cast<int>(firstLn.size()));

  firstLn = firstLn.substr(0, beginCol) + boundaryLn;
  lines.erase(lines.begin() + r.begin.line + 1, lines.begin() + r.endLine + 1);

  if (firstLn.empty() && lines.size() > 1 && mode != Mode::Insert) {
    lines.erase(lines.begin() + r.begin.line);
  }

  assert(!lines.empty());
}

void applyLineCharDeletionToBuffer(Lines& lines, const LineCharRange& r,
                                   const CursorPos& originalPos, Mode mode) {
  assert(r.beginLine >= 0 && r.beginLine < static_cast<int>(lines.size()));
  assert(r.end.line >= r.beginLine && r.end.line < static_cast<int>(lines.size()));
  assert(r.end.col >= 0 && r.end.col <= static_cast<int>(lines[r.end.line].size()));

  applyCharDeletionToBuffer(lines, CharRange(CursorPos(r.beginLine, 0), r.end), originalPos, mode);
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

}  // namespace

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

void deleteRangeAndUpdatePos(Lines& lines, const CharRange& range, CursorPos& pos, Mode mode) {
  assert(range.isValid());
  if (range.isEmpty()) return;
  CursorPos originalPos = pos;
  applyCharDeletionToBuffer(lines, range, originalPos, mode);
  placeCursorAfterCharDeletion(lines, range.begin.line, range.begin.col, pos, mode);
}

void deleteCharLineRangeAndUpdatePos(Lines& lines, const CharLineRange& range,
                                     CursorPos& pos, Mode mode) {
  assert(range.isValid());
  applyCharLineDeletionToBuffer(lines, range, mode);
  placeCursorAfterCharDeletion(lines, range.begin.line, range.begin.col, pos, mode);
}

void deleteLineCharRangeAndUpdatePos(Lines& lines, const LineCharRange& range,
                                     CursorPos& pos, Mode mode) {
  assert(range.isValid());
  CursorPos originalPos = pos;
  applyLineCharDeletionToBuffer(lines, range, originalPos, mode);
  placeCursorAfterCharDeletion(lines, range.beginLine, 0, pos, mode);
}

void deleteLineRangeAndUpdatePos(Lines& lines, const LineRange& range, CursorPos& pos,
                                 bool hasLinesBelow) {
  assert(range.isValid());

  assert(range.beginLine >= 0 && range.beginLine < static_cast<int>(lines.size()));
  assert(range.endLine > range.beginLine && range.endLine <= static_cast<int>(lines.size()));

  lines.erase(lines.begin() + range.beginLine, lines.begin() + range.endLine);

  // Maintain invariant: buffer always has at least one line
  if (lines.empty()) {
    lines.push_back("");
  }

  int newSize = static_cast<int>(lines.size());

  // If deletion removed the last lines and there are lines below in the real
  // buffer, cursor goes to the line below (past the end of effective lines).
  // Column is left unset — caller must apply 'k' to bring it back in range.
  if (hasLinesBelow && range.beginLine >= newSize) {
    pos.line = range.beginLine;  // Past end of effective lines
    return;
  }

  pos.line = min(range.beginLine, newSize - 1);
  if constexpr (VimOptions::startOfLine()) {
    // Legacy Vim: dd goes to first non-blank of the new current line
    pos.setCol(firstNonBlankColInLineStr(lines[pos.line]));
  } else {
    // Neovim: dd resets targetCol to the clamped column
    if (lines[pos.line].empty()) {
      pos.setCol(0);
    } else {
      pos.setCol(min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
    }
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
  size_t start = 0;

  // Only strip leading whitespace for J command (addSpace=true)
  // gJ (addSpace=false) preserves all whitespace per Vim docs
  if (addSpace) {
    while (start < nextLine.size() &&
           (nextLine[start] == ' ' || nextLine[start] == '\t')) {
      start++;
    }
  }

  // For J command: add space only if first line doesn't already end with whitespace
  // Vim does NOT strip trailing whitespace - it keeps it and only adds space when needed
  if (addSpace && !currentLine.empty() && start < nextLine.size()) {
    bool hasTrailingWhitespace = (currentLine.back() == ' ' || currentLine.back() == '\t');
    if (!hasTrailingWhitespace) {
      // joinspaces: add 2 spaces after .!? (Vim default), else single space (Neovim default)
      bool needsTwoSpaces = VimOptions::joinSpaces() &&
                            (currentLine.back() == '.' || currentLine.back() == '!' || currentLine.back() == '?');
      if (needsTwoSpaces) {
        currentLine += "  ";
      } else {
        currentLine += ' ';
      }
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
