#include "VimEditUtils.h"
#include "VimOptions.h"
#include "VimUtils.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace VimEditUtils {

// -----------------------------------------------------------------------------
// Multi-line operations
// -----------------------------------------------------------------------------

void deleteRange(Lines& lines, const Range& range, Position& pos, Mode mode) {
  if (lines.empty()) {
    pos = {0, 0};
    return;
  }

  Range r = range;
  r.normalize();

  if (r.linewise) {
    assert(r.start.line >= 0 && r.start.line < static_cast<int>(lines.size()));
    assert(r.end.line >= 0 && r.end.line < static_cast<int>(lines.size()));

    lines.erase(lines.begin() + r.start.line, lines.begin() + r.end.line + 1);

    // Allow truly empty buffer - symmetric with empty string
    pos.line = lines.empty() ? 0 : min(r.start.line, static_cast<int>(lines.size()) - 1);
    pos.col = lines.empty() ? 0 : VimUtils::firstNonBlankColInLineStr(lines[pos.line]);

  } else {
    // Character-wise deletion
    assert(r.start.line >= 0 && r.start.line < static_cast<int>(lines.size()));
    assert(r.end.line >= 0 && r.end.line < static_cast<int>(lines.size()));

    int endCol = r.inclusive ? r.end.col + 1 : r.end.col;

    if (r.start.line == r.end.line) {
      // Single line deletion
      string& ln = lines[r.start.line];
      assert(r.start.col >= 0 && r.start.col <= static_cast<int>(ln.size()));
      endCol = min(endCol, static_cast<int>(ln.size()));
      ln.erase(r.start.col, endCol - r.start.col);
    } else {
      // Multi-line deletion: merge first and last line, delete lines in between
      string& firstLn = lines[r.start.line];
      const string& lastLn = lines[r.end.line];

      assert(r.start.col >= 0 && r.start.col <= static_cast<int>(firstLn.size()));
      endCol = min(endCol, static_cast<int>(lastLn.size()));

      // Merge: keep first part of first line + last part of last line
      firstLn = firstLn.substr(0, r.start.col) + lastLn.substr(endCol);

      // Delete lines from startLine+1 to endLine (inclusive)
      lines.erase(lines.begin() + r.start.line + 1, lines.begin() + r.end.line + 1);
    }

    pos.line = r.start.line;
    pos.col = r.start.col;
    if (mode == Mode::Insert) {
      clampInsertCol(lines[pos.line], pos.col);
    } else {
      clampCol(lines[pos.line], pos.col);
    }
  }
}

void insertText(Lines& lines, Position& pos, const string& text) {
  if (text.empty()) return;

  // Empty buffer: inserting text creates content
  if (lines.empty()) {
    lines.push_back("");
    pos = {0, 0};
  }

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

void joinLines(Lines& lines, Position& pos, bool addSpace) {
  assert (pos.line+1 < lines.size());

  string& currentLine = lines[pos.line];
  int joinCol = static_cast<int>(currentLine.size());

  // Remove trailing whitespace from current line if adding space (J command)
  if (addSpace) {
    while (!currentLine.empty() &&
           (currentLine.back() == ' ' || currentLine.back() == '\t')) {
      currentLine.pop_back();
    }
    joinCol = static_cast<int>(currentLine.size());
  }

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

  // Join with optional space (only for J when both lines have content)
  if (addSpace && !currentLine.empty() && start < nextLine.size()) {
    // joinspaces: add 2 spaces after .!? (Vim default), else single space (Neovim default)
    bool needsTwoSpaces = VimOptions::joinSpaces() && !currentLine.empty() &&
                          (currentLine.back() == '.' || currentLine.back() == '!' || currentLine.back() == '?');
    if (needsTwoSpaces) {
      currentLine += "  ";
      joinCol += 2;
    } else {
      currentLine += ' ';
      joinCol++;
    }
  }
  currentLine += nextLine.substr(start);

  // Remove the next line
  lines.erase(lines.begin() + pos.line + 1);

  pos.col = joinCol > 0 ? joinCol - 1 : 0;
}

void openLineBelow(Lines& lines, Position& pos) {
  assert(!lines.empty());
  assert(pos.line >= 0 && pos.line < static_cast<int>(lines.size()));

  lines.insert(lines.begin() + pos.line + 1, "");
  pos = Position(pos.line + 1, 0);
}

void openLineAbove(Lines& lines, Position& pos) {
  assert(!lines.empty());
  assert(pos.line >= 0 && pos.line < static_cast<int>(lines.size()));

  lines.insert(lines.begin() + pos.line, "");
  pos.col = 0;
  // pos.line stays the same (now points to the new empty line)
}

// -----------------------------------------------------------------------------
// Single-line operations
// -----------------------------------------------------------------------------

void eraseToEnd(string& line, int& col) {
  assert(!line.empty() && col >= 0 && col < static_cast<int>(line.size()));

  line.erase(col);
  col = line.empty() ? 0 : static_cast<int>(line.size()) - 1;
}

} // namespace VimEditUtils
