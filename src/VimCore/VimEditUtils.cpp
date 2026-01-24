#include "VimEditUtils.h"
#include "VimOptions.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace VimCore {

// =============================================================================
// Multi-Line Buffer Operations
// =============================================================================

void deleteRange(Lines& lines, const Range& range, Position& pos, Mode mode) {
  Range r = range;
  r.normalize();

  // Determine if cursor is on the deletion line BEFORE modifying pos.
  // This affects empty line removal behavior:
  // - Cursor on same line (D at col 0): keep empty line
  // - Cursor on different line (db from col 0): remove empty line
  bool cursorOnDeletionLine = (pos.line == r.first.line);

  int endCol = r.last.col + 1;  // Inclusive: delete up to and including end.col

  if (r.first.line == r.last.line) {
    // Single line deletion
    string& ln = lines[r.first.line];
    endCol = min(endCol, static_cast<int>(ln.size()));
    ln.erase(r.first.col, endCol - r.first.col);

    // Vim behavior for empty lines after single-line deletion:
    // - If cursor was on the same line (D at col 0): keep empty line
    // - If cursor was on different line (db from col 0): remove empty line
    if (ln.empty() && r.first.col == 0 && lines.size() > 1 && !cursorOnDeletionLine) {
      lines.erase(lines.begin() + r.first.line);
    }
  } else {
    // Multi-line deletion: merge first and last line, delete lines in between
    string& firstLn = lines[r.first.line];
    const string& lastLn = lines[r.last.line];

    endCol = min(endCol, static_cast<int>(lastLn.size()));

    // Merge: keep first part of first line + last part of last line
    firstLn = firstLn.substr(0, r.first.col) + lastLn.substr(endCol);

    // Delete lines from startLine+1 to endLine (inclusive)
    lines.erase(lines.begin() + r.first.line + 1, lines.begin() + r.last.line + 1);

    // Vim behavior: if multi-line deletion results in empty merged line AND
    // there are other lines in the buffer, remove the empty line.
    // This matches neovim's behavior where `de` on a single-char line followed
    // by other content removes the line entirely rather than leaving it empty.
    if (firstLn.empty() && lines.size() > 1) {
      lines.erase(lines.begin() + r.first.line);
    }

    assert(!lines.empty());
  }

  pos.line = r.first.line;
  // Clamp position to valid range after possible line removal
  if (pos.line >= static_cast<int>(lines.size())) {
    pos.line = static_cast<int>(lines.size()) - 1;
  }
  // Compute clamped column and update both col and targetCol
  int newCol = r.first.col;
  if (mode == Mode::Insert) {
    newCol = min(newCol, static_cast<int>(lines[pos.line].size()));
  } else {
    newCol = lines[pos.line].empty() ? 0 : min(newCol, static_cast<int>(lines[pos.line].size()) - 1);
  }
  pos.setCol(newCol);
}

void deleteRangeLinewise(Lines& lines, const LineRange& range, Position& pos) {
  LineRange r = range;
  r.normalize();

  assert(r.firstLine >= 0 && r.firstLine < static_cast<int>(lines.size()));
  assert(r.lastLine >= 0 && r.lastLine < static_cast<int>(lines.size()));

  lines.erase(lines.begin() + r.firstLine, lines.begin() + r.lastLine + 1);

  // Maintain invariant: buffer always has at least one line
  if (lines.empty()) {
    lines.push_back("");
  }

  pos.line = min(r.firstLine, static_cast<int>(lines.size()) - 1);
  // dd resets targetCol to the clamped column (unlike vertical motions which preserve it)
  if (lines[pos.line].empty()) {
    pos.setCol(0);
  } else {
    pos.setCol(min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
  }
}

void insertText(Lines& lines, Position& pos, const string& text) {
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

  // J: cursor on inserted space (joinCol - 1)
  // gJ: cursor on first char of joined content (joinCol)
  if (addSpace) {
    pos.setCol(joinCol > 0 ? joinCol - 1 : 0);
  } else {
    pos.setCol(joinCol);
  }
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
  pos.setCol(0);
  // pos.line stays the same (now points to the new empty line)
}

} // namespace VimCore
