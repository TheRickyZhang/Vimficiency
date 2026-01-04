#include "Edit.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimUtils.h"

#include <cassert>
#include <stdexcept>

using namespace std;

namespace Edit {

// Compile-time string hash for switch statements.
// Uses M=131 (prime > 126) to guarantee collision-free hashing for all strings.
// Math: hash(s) = s[0]*M^(n-1) + s[1]*M^(n-2) + ... + s[n-1]
// This is bijective (unique hash per string) until 64-bit overflow at ~10 chars.
constexpr size_t hash(string_view s) {
  assert(s.size() <= 10 && "hash() only collision-free for strings <= 10 chars");
  size_t h = 0;
  for (char c : s) h = h * 131 + static_cast<unsigned char>(c);
  return h;
}

// -----------------------------------------------------------------------------
// Operator + Range operations (called directly, not through applyEdit)
// -----------------------------------------------------------------------------

void deleteRange(Lines& lines, Position& pos, Mode mode, const Range& range) {
  assert(mode == Mode::Normal);
  VimEditUtils::deleteRange(lines, range, pos);
}

void changeRange(Lines& lines, Position& pos, Mode& mode, const Range& range) {
  assert(mode == Mode::Normal);
  VimEditUtils::deleteRange(lines, range, pos);
  mode = Mode::Insert;
}

void yankRange(Lines& lines, Position& pos, Mode mode, const Range& range) {
  assert(mode == Mode::Normal);
  Range r = range;
  r.normalize();
  if (r.linewise) {
    pos.line = r.start.line;
    pos.col = VimUtils::firstNonBlankColInLineStr(lines[r.start.line]);
  } else {
    pos = r.start;
  }
}

// -----------------------------------------------------------------------------
// Insert mode text insertion (called directly for typed characters)
// -----------------------------------------------------------------------------

void insertText(Lines& lines, Position& pos, Mode mode, const string& text) {
  assert(mode == Mode::Insert);
  VimEditUtils::insertText(lines, pos, text);
}

// -----------------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------------

void applyEdit(Lines& lines, Position& pos, Mode& mode,
               const NavContext& navContext,
               const ParsedEdit& edit) {
  string_view e = edit.edit;
  int count = edit.effectiveCount();

  if (mode == Mode::Normal) {
    // Handle r{char} specially (variable second character)
    if (e.size() == 2 && e[0] == 'r') {
      char newChar = e[1];
      for (int i = 0; i < count; i++) {
        VimEditUtils::replaceChar(lines, pos, newChar);
        if (i < count - 1 && pos.col + 1 < static_cast<int>(lines[pos.line].size())) {
          pos.col++;
        }
      }
      return;
    }

    switch (hash(e)) {
      case hash("x"):
        for (int i = 0; i < count; i++) VimEditUtils::deleteChar(lines, pos);
        return;

      case hash("X"):
        for (int i = 0; i < count; i++) VimEditUtils::deleteCharBefore(lines, pos);
        return;

      case hash("~"):
        for (int i = 0; i < count; i++) VimEditUtils::toggleCase(lines, pos);
        return;

      case hash("D"):
        VimEditUtils::deleteToEndOfLine(lines, pos);
        return;

      case hash("C"):
        VimEditUtils::changeToEndOfLine(lines, pos);
        mode = Mode::Insert;
        return;

      case hash("J"):
        for (int i = 0; i < count; i++) VimEditUtils::joinLines(lines, pos, true);
        return;

      case hash("gJ"):
        for (int i = 0; i < count; i++) VimEditUtils::joinLines(lines, pos, false);
        return;

      case hash("dd"):
        for (int i = 0; i < count; i++) VimEditUtils::deleteLine(lines, pos);
        return;

      case hash("cc"):
      case hash("S"):
        VimEditUtils::clearLine(lines, pos);
        mode = Mode::Insert;
        return;

      case hash("o"):
        VimEditUtils::openLineBelow(lines, pos);
        mode = Mode::Insert;
        return;

      case hash("O"):
        VimEditUtils::openLineAbove(lines, pos);
        mode = Mode::Insert;
        return;

      case hash("s"):
        VimEditUtils::deleteChar(lines, pos);
        mode = Mode::Insert;
        return;

      case hash("i"):
        mode = Mode::Insert;
        return;

      case hash("I"):
        pos.col = VimUtils::firstNonBlankColInLineStr(lines[pos.line]);
        mode = Mode::Insert;
        return;

      case hash("a"):
        {
          int len = static_cast<int>(lines[pos.line].size());
          pos.col = len > 0 ? min(pos.col + 1, len) : 0;
        }
        mode = Mode::Insert;
        return;

      case hash("A"):
        pos.col = static_cast<int>(lines[pos.line].size());
        mode = Mode::Insert;
        return;
    }
  }

  if (mode == Mode::Insert) {
    switch (hash(e)) {
      case hash("<Esc>"):
        if (pos.col > 0) pos.col--;
        mode = Mode::Normal;
        return;

      case hash("<BS>"):
        if (pos.col == 0) {
          if (pos.line > 0) {
            int prevLen = static_cast<int>(lines[pos.line - 1].size());
            Position joinPos(pos.line - 1, 0);
            VimEditUtils::joinLines(lines, joinPos, false);
            pos = Position(pos.line - 1, prevLen);
          }
        } else {
          VimEditUtils::deleteCharBefore(lines, pos);
        }
        return;

      case hash("<Del>"):
        if (lines.empty()) return;
        {
          int len = static_cast<int>(lines[pos.line].size());
          if (pos.col >= len) {
            int n = static_cast<int>(lines.size());
            if (pos.line < n - 1) {
              VimEditUtils::joinLines(lines, pos, false);
            }
          } else {
            Position delPos = pos;
            VimEditUtils::deleteChar(lines, delPos);
          }
        }
        return;

      case hash("<CR>"):
        VimEditUtils::insertText(lines, pos, "\n");
        return;

      case hash("<C-u>"):
        // Delete from cursor to start of line
        if (pos.col > 0) {
          VimEditUtils::deleteText(lines, Position(pos.line, 0), pos, pos);
        }
        return;

      case hash("<C-w>"):
        // Delete word before cursor
        if (pos.col > 0) {
          int col = pos.col - 1;
          const string& line = lines[pos.line];
          // Skip whitespace backwards
          while (col > 0 && VimUtils::isBlank(line[col])) col--;
          // Delete word chars backwards
          if (VimUtils::isSmallWordChar(line[col])) {
            while (col > 0 && VimUtils::isSmallWordChar(line[col - 1])) col--;
          } else if (!VimUtils::isBlank(line[col])) {
            // Non-word, non-blank: delete punctuation sequence
            while (col > 0 && !VimUtils::isSmallWordChar(line[col - 1]) &&
                   !VimUtils::isBlank(line[col - 1])) col--;
          }
          VimEditUtils::deleteText(lines, Position(pos.line, col), pos, pos);
        }
        return;
    }
  }

  throw runtime_error("Unknown edit: " + string(e));
}

} // namespace Edit
