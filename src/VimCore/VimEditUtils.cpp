#include "VimEditUtils.h"
#include "VimUtils.h"

#include <algorithm>
#include <cctype>

using namespace std;

namespace VimEditUtils {

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

char charAt(const Lines& lines, Position pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return '\0';
  const string& ln = lines[pos.line];
  if (pos.col < 0 || pos.col >= static_cast<int>(ln.size())) return '\0';
  return ln[pos.col];
}

bool isValidPosition(const Lines& lines, Position pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return false;
  const string& ln = lines[pos.line];
  return pos.col >= 0 && pos.col < static_cast<int>(ln.size());
}

Position clampPosition(const Lines& lines, Position pos) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Position(0, 0);

  pos.line = clamp(pos.line, 0, n - 1);
  int len = static_cast<int>(lines[pos.line].size());
  pos.col = len > 0 ? clamp(pos.col, 0, len - 1) : 0;
  return pos;
}

// -----------------------------------------------------------------------------
// Range-based operations
// -----------------------------------------------------------------------------

void deleteRange(Lines& lines, const Range& range, Position& pos) {
  if (lines.empty()) {
    pos = Position(0, 0);
    return;
  }

  Range r = range;
  r.normalize();

  if (r.linewise) {
    // Delete entire lines from start.line to end.line
    int startLine = clamp(r.start.line, 0, static_cast<int>(lines.size()) - 1);
    int endLine = clamp(r.end.line, 0, static_cast<int>(lines.size()) - 1);

    // Erase the lines
    lines.erase(lines.begin() + startLine, lines.begin() + endLine + 1);

    // If all lines deleted, keep one empty line
    if (lines.empty()) {
      lines.push_back("");
    }

    // Cursor goes to first non-blank of line at deletion point
    int newLine = min(startLine, static_cast<int>(lines.size()) - 1);
    pos.line = newLine;
    pos.col = VimUtils::firstNonBlankColInLineStr(lines[newLine]);

  } else {
    // Character-wise deletion
    int startLine = clamp(r.start.line, 0, static_cast<int>(lines.size()) - 1);
    int endLine = clamp(r.end.line, 0, static_cast<int>(lines.size()) - 1);

    int startCol = r.start.col;
    int endCol = r.inclusive ? r.end.col + 1 : r.end.col;

    if (startLine == endLine) {
      // Single line deletion
      string& ln = lines[startLine];
      int sc = clamp(startCol, 0, static_cast<int>(ln.size()));
      int ec = clamp(endCol, 0, static_cast<int>(ln.size()));
      ln.erase(sc, ec - sc);
    } else {
      // Multi-line deletion: merge first and last line, delete lines in between
      string& firstLn = lines[startLine];
      const string& lastLn = lines[endLine];
      int sc = clamp(startCol, 0, static_cast<int>(firstLn.size()));
      int ec = r.inclusive ? r.end.col + 1 : r.end.col;
      ec = clamp(ec, 0, static_cast<int>(lastLn.size()));

      // Merge: keep first part of first line + last part of last line
      firstLn = firstLn.substr(0, sc) + lastLn.substr(ec);

      // Delete lines from startLine+1 to endLine (inclusive)
      lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
    }

    pos.line = startLine;
    pos.col = startCol;
    // Clamp to valid position
    if (!lines.empty()) {
      pos = clampPosition(lines, pos);
    }
  }
}

void replaceRange(Lines& lines, const Range& range,
                  const string& replacement, Position& pos) {
  // First delete the range
  deleteRange(lines, range, pos);

  // Then insert the replacement at pos
  insertText(lines, pos, replacement);
}

// -----------------------------------------------------------------------------
// Single character operations
// -----------------------------------------------------------------------------

void deleteChar(Lines& lines, Position& pos) {
  if (lines.empty() || !isValidPosition(lines, pos)) {
    pos = clampPosition(lines, pos);
    return;
  }

  Range r(pos, pos, false, true);
  deleteRange(lines, r, pos);
}

void deleteCharBefore(Lines& lines, Position& pos) {
  if (lines.empty() || pos.col <= 0) {
    pos = clampPosition(lines, pos);
    return;
  }

  Position beforePos(pos.line, pos.col - 1);
  Range r(beforePos, beforePos, false, true);
  deleteRange(lines, r, pos);
}

void replaceChar(Lines& lines, Position& pos, char newChar) {
  if (lines.empty() || !isValidPosition(lines, pos)) {
    pos = clampPosition(lines, pos);
    return;
  }

  lines[pos.line][pos.col] = newChar;
  // pos stays the same
}

void toggleCase(Lines& lines, Position& pos) {
  if (lines.empty() || !isValidPosition(lines, pos)) {
    pos = clampPosition(lines, pos);
    return;
  }

  char c = lines[pos.line][pos.col];
  if (isupper(c)) {
    lines[pos.line][pos.col] = tolower(c);
  } else if (islower(c)) {
    lines[pos.line][pos.col] = toupper(c);
  }

  // Move cursor forward (Vim behavior)
  if (pos.col + 1 < static_cast<int>(lines[pos.line].size())) {
    pos.col++;
  }
}

// -----------------------------------------------------------------------------
// Line operations
// -----------------------------------------------------------------------------

void deleteLine(Lines& lines, Position& pos) {
  if (lines.empty()) {
    pos = Position(0, 0);
    return;
  }

  int lineIdx = clamp(pos.line, 0, static_cast<int>(lines.size()) - 1);
  Range r(Position(lineIdx, 0), Position(lineIdx, 0), true, true);
  deleteRange(lines, r, pos);
}

void deleteToEndOfLine(Lines& lines, Position& pos) {
  if (lines.empty()) {
    pos = Position(0, 0);
    return;
  }

  pos = clampPosition(lines, pos);
  string& ln = lines[pos.line];

  if (ln.empty() || pos.col >= static_cast<int>(ln.size())) {
    return;
  }

  // Erase from pos.col to end
  ln.erase(pos.col);

  // Clamp pos if needed
  if (!ln.empty() && pos.col >= static_cast<int>(ln.size())) {
    pos.col = static_cast<int>(ln.size()) - 1;
  }
}

void joinLines(Lines& lines, Position& pos, bool addSpace) {
  int n = static_cast<int>(lines.size());
  int lineIdx = pos.line;
  if (n == 0 || lineIdx < 0 || lineIdx >= n - 1) {
    pos = clampPosition(lines, pos);
    return;
  }

  string& currentLine = lines[lineIdx];
  int joinCol = static_cast<int>(currentLine.size());

  // Remove trailing whitespace from current line if adding space
  if (addSpace) {
    while (!currentLine.empty() && (currentLine.back() == ' ' || currentLine.back() == '\t')) {
      currentLine.pop_back();
    }
    joinCol = static_cast<int>(currentLine.size());
  }

  // Get next line, stripping leading whitespace
  string nextLine = lines[lineIdx + 1];
  size_t start = 0;
  while (start < nextLine.size() && (nextLine[start] == ' ' || nextLine[start] == '\t')) {
    start++;
  }

  // Join with optional space
  if (addSpace && !currentLine.empty() && start < nextLine.size()) {
    currentLine += ' ';
    joinCol++;
  }
  currentLine += nextLine.substr(start);

  // Remove the next line
  lines.erase(lines.begin() + lineIdx + 1);

  pos = Position(lineIdx, joinCol > 0 ? joinCol - 1 : 0);
}

void openLineBelow(Lines& lines, Position& pos) {
  int n = static_cast<int>(lines.size());
  int lineIdx = n > 0 ? clamp(pos.line, 0, n - 1) : 0;

  if (n == 0) {
    lines.push_back("");
    lines.push_back("");
    pos = Position(1, 0);
    return;
  }

  // Insert empty line after lineIdx
  lines.insert(lines.begin() + lineIdx + 1, "");
  pos = Position(lineIdx + 1, 0);
}

void openLineAbove(Lines& lines, Position& pos) {
  int n = static_cast<int>(lines.size());
  int lineIdx = n > 0 ? clamp(pos.line, 0, n - 1) : 0;

  if (n == 0) {
    lines.push_back("");
    lines.push_back("");
    pos = Position(0, 0);
    return;
  }

  // Insert empty line before lineIdx
  lines.insert(lines.begin() + lineIdx, "");
  pos = Position(lineIdx, 0);
}

void clearLine(Lines& lines, Position& pos) {
  if (lines.empty()) {
    lines.push_back("");
    pos = Position(0, 0);
    return;
  }

  int lineIdx = clamp(pos.line, 0, static_cast<int>(lines.size()) - 1);
  lines[lineIdx].clear();
  pos = Position(lineIdx, 0);
}

void changeToEndOfLine(Lines& lines, Position& pos) {
  deleteToEndOfLine(lines, pos);
}

// -----------------------------------------------------------------------------
// Insert mode text manipulation
// -----------------------------------------------------------------------------

void insertText(Lines& lines, Position& pos, const string& text) {
  if (text.empty()) {
    return;
  }

  int n = static_cast<int>(lines.size());

  if (n == 0) {
    // Start with empty buffer
    lines.push_back("");
    n = 1;
    pos = Position(0, 0);
  }

  pos.line = clamp(pos.line, 0, n - 1);
  pos.col = clamp(pos.col, 0, static_cast<int>(lines[pos.line].size()));

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
    pos.col += static_cast<int>(text.size());
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
        pos = Position(insertPos, static_cast<int>(textLines[i].size()));
      } else {
        lines.insert(lines.begin() + insertPos, textLines[i]);
      }
      insertPos++;
    }
  }
}

void deleteText(Lines& lines, Position start, Position end, Position& pos) {
  Range r(start, end, false, false);  // Non-inclusive end
  deleteRange(lines, r, pos);
}

} // namespace VimEditUtils
