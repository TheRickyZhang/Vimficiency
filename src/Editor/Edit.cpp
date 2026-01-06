#include "Edit.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimMovementUtils.h"
#include "VimCore/VimUtils.h"
#include "Utils/Debug.h"

#include <cassert>
#include <stdexcept>
#include <algorithm>

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

inline void clampToLastChar(const string& line, int& col) {
  col = line.empty() ? 0 : min(col, (int)line.size() - 1);
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

// Error on actions that don't do anything - these should be pruned by search logic.
// Note: something like 3dw on a line may do nothing on 2nd/3rd dw, but since
// we can't prune that easily without doing equivalent work as the action, it's fine.
// TODO: we can apply the same technique to Motions as well
void applyEdit(Lines& lines, Position& pos, Mode& mode,
               const NavContext& navContext,
               const ParsedEdit& edit) {
  string_view e = edit.edit;
  int count = edit.effectiveCount();
  size_t h = hash(e);

  // Empty buffer: only switch to insert mode
  if (lines.empty()) {
    if (mode == Mode::Normal) {
      switch (h) {
        case hash("i"): case hash("a"):
          mode = Mode::Insert;
          return;
        // Same effect for both on empty - create first line
        case hash("o"): case hash("O"):
          lines.push_back("");
          pos = {0, 0};
          mode = Mode::Insert;
          return;
      }
    }
    throw runtime_error("Edit '" + string(e) + "' invalid on empty buffer");
  }

  string& line = lines[pos.line];
  int n = static_cast<int>(lines.size());
  int m = static_cast<int>(line.size());

  // Empty line: only switch to insert mode, vertical motion
  if (line.empty() && mode == Mode::Normal) {
    switch (h) {
      case hash("i"): case hash("a"): case hash("I"): case hash("A"):
      // Line operations (valid)
      case hash("o"): case hash("O"): case hash("dd"): case hash("cc"): case hash("S"):
      case hash("J"): case hash("gJ"):
        break;  // Fall through to main switch
      default:
        throw runtime_error("Edit '" + string(e) + "' invalid on empty line");
    }
  }

  if (mode == Mode::Normal) {
    // Handle r{char} specially. Recall this fails if not enough characters.
    if (e.size() == 2 && e[0] == 'r') {
      if (pos.col + count > m) {
        throw runtime_error("r{char} requires " + to_string(count) + " chars but only " + to_string(m - pos.col) + " available");
      }
      char newChar = e[1];
      for (int i = 0; i < count; i++) {
        line[pos.col + i] = newChar;
      }
      pos.col += count - 1;
      return;
    }

    switch (hash(e)) {
      case hash("x"):
        if (pos.col + count > m) {
          throw runtime_error("x requires " + to_string(count) + " chars");
        }
        line.erase(pos.col, count);
        clampToLastChar(line, pos.col);
        return;

      case hash("X"):
        if (count > pos.col) {
          throw runtime_error("X requires " + to_string(count) + " chars before cursor");
        }
        line.erase(pos.col - count, count);
        pos.col -= count;
        return;

      case hash("~"):
        if (pos.col + count > m) {
          throw runtime_error("~ requires " + to_string(count) + " chars");
        }
        for (int i = 0; i < count; i++) {
          char& c = line[pos.col + i];
          if (isupper(c)) c = tolower(c);
          else if (islower(c)) c = toupper(c);
        }
        pos.col += count - 1;
        return;

      case hash("D"):
        VimEditUtils::eraseToEnd(line, pos.col);
        return;

      case hash("C"):
        VimEditUtils::eraseToEnd(line, pos.col);
        mode = Mode::Insert;
        return;

      case hash("J"):
        if (pos.line + count >= n) {
          throw runtime_error("J requires " + to_string(count) + " lines below");
        }
        for (int i = 0; i < count; i++) VimEditUtils::joinLines(lines, pos, true);
        return;

      case hash("gJ"):
        if (pos.line + count >= n) {
          throw runtime_error("gJ requires " + to_string(count) + " lines below");
        }
        for (int i = 0; i < count; i++) VimEditUtils::joinLines(lines, pos, false);
        return;

      case hash("dd"):
        if (pos.line + count > n) {
          throw runtime_error("dd requires " + to_string(count) + " lines");
        }
        lines.erase(lines.begin() + pos.line, lines.begin() + pos.line + count);
        pos.line = lines.empty() ? 0 : min(pos.line, static_cast<int>(lines.size()) - 1);
        // 'startofline' behavior: go to first non-blank, update targetCol
        pos.setCol(lines.empty() ? 0 : VimUtils::firstNonBlankColInLineStr(lines[pos.line]));
        return;

      case hash("cc"): case hash("S"):
        line.clear();
        pos.col = 0;
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
        if (pos.col + count > m) {
          throw runtime_error("s requires " + to_string(count) + " chars");
        }
        line.erase(pos.col, count);
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
        pos.col++;
        mode = Mode::Insert;
        return;

      case hash("A"):
        pos.col = m;
        mode = Mode::Insert;
        return;

      // --- Word deletion motions (d + motion) ---
      // Note: These silently do nothing if motion doesn't move.
      // This is less strict than char operations but motion no-ops are harder to predict.
      case hash("dw"):
      case hash("dW"):
        {
          bool big = (e == "dW");
          Position endPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionW(endPos, lines, big);
          if (endPos.line > pos.line || endPos.col > pos.col) {
            // dw is exclusive, so Range end is endPos with inclusive=false
            Range r(pos, Position(endPos.line, endPos.col > 0 ? endPos.col - 1 : 0), false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
        }
        return;

      case hash("de"):
      case hash("dE"):
        {
          bool big = (e == "dE");
          Position endPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionE(endPos, lines, big);
          if (endPos != pos) {
            Range r(pos, endPos, false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
        }
        return;

      case hash("db"):
      case hash("dB"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        {
          bool big = (e == "dB");
          Position startPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionB(startPos, lines, big);
          if (startPos < pos) {
            Range r(startPos, Position(pos.line, pos.col > 0 ? pos.col - 1 : 0), false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
        }
        return;

      case hash("dge"):
      case hash("dgE"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        {
          bool big = (e == "dgE");
          Position startPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionGe(startPos, lines, big);
          if (startPos < pos) {
            Range r(startPos, Position(pos.line, pos.col > 0 ? pos.col - 1 : 0), false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
        }
        return;

      case hash("d0"):
        if (count > 1) {
          debug("d0: count", count, "ignored (0 motion doesn't use count)");
        }
        if (pos.col == 0) {
          throw runtime_error("d0 at column 0 has no effect");
        }
        {
          Range r(Position(pos.line, 0), Position(pos.line, pos.col - 1), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        return;

      case hash("d^"):
        if (count > 1) {
          debug("d^: count", count, "ignored (^ motion doesn't use count)");
        }
        {
          int firstNonBlank = VimUtils::firstNonBlankColInLineStr(lines[pos.line]);
          if (firstNonBlank >= pos.col) {
            throw runtime_error("d^ at or before first non-blank has no effect");
          }
          Range r(Position(pos.line, firstNonBlank), Position(pos.line, pos.col - 1), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        return;

      // --- Change motions (c + motion) ---
      // Note: These silently enter insert mode even if motion doesn't move.
      case hash("cw"):
      case hash("cW"):
        {
          bool big = (e == "cW");
          Position endPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionW(endPos, lines, big);
          if (endPos.line > pos.line || endPos.col > pos.col) {
            Range r(pos, Position(endPos.line, endPos.col > 0 ? endPos.col - 1 : 0), false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
          mode = Mode::Insert;
        }
        return;

      case hash("ce"):
      case hash("cE"):
        {
          bool big = (e == "cE");
          Position endPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionE(endPos, lines, big);
          if (endPos != pos) {
            Range r(pos, endPos, false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
          mode = Mode::Insert;
        }
        return;

      case hash("cb"):
      case hash("cB"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        {
          bool big = (e == "cB");
          Position startPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionB(startPos, lines, big);
          if (startPos < pos) {
            Range r(startPos, Position(pos.line, pos.col > 0 ? pos.col - 1 : 0), false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
          mode = Mode::Insert;
        }
        return;

      case hash("cge"):
      case hash("cgE"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        {
          bool big = (e == "cgE");
          Position startPos = pos;
          for (int i = 0; i < count; i++) VimMovementUtils::motionGe(startPos, lines, big);
          if (startPos < pos) {
            Range r(startPos, Position(pos.line, pos.col > 0 ? pos.col - 1 : 0), false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
          mode = Mode::Insert;
        }
        return;

      case hash("c0"):
        if (count > 1) {
          debug("c0: count", count, "ignored (0 motion doesn't use count)");
        }
        if (pos.col == 0) {
          throw runtime_error("c0 at column 0 has no effect");
        }
        {
          Range r(Position(pos.line, 0), Position(pos.line, pos.col - 1), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        mode = Mode::Insert;
        return;

      case hash("c^"):
        if (count > 1) {
          debug("c^: count", count, "ignored (^ motion doesn't use count)");
        }
        {
          int firstNonBlank = VimUtils::firstNonBlankColInLineStr(lines[pos.line]);
          if (firstNonBlank >= pos.col) {
            throw runtime_error("c^ at or before first non-blank has no effect");
          }
          Range r(Position(pos.line, firstNonBlank), Position(pos.line, pos.col - 1), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        mode = Mode::Insert;
        return;

      case hash("c$"):
        if (pos.line + count > n) {
          throw runtime_error("c$ requires " + to_string(count) + " lines but only " + to_string(n - pos.line) + " available");
        }
        {
          int endLine = pos.line + count - 1;
          int endCol = lines[endLine].empty() ? 0 : static_cast<int>(lines[endLine].size()) - 1;
          Range r(pos, Position(endLine, endCol), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
          mode = Mode::Insert;
        }
        return;

      case hash("d$"):
        if (pos.line + count > n) {
          throw runtime_error("d$ requires " + to_string(count) + " lines but only " + to_string(n - pos.line) + " available");
        }
        {
          int endLine = pos.line + count - 1;
          int endCol = lines[endLine].empty() ? 0 : static_cast<int>(lines[endLine].size()) - 1;
          Range r(pos, Position(endLine, endCol), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
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
        if (pos.col == 0 && pos.line == 0) {
          throw runtime_error("<BS> at start of buffer has no effect");
        }
        if (pos.col == 0) {
          // Join with previous line
          int prevLen = static_cast<int>(lines[pos.line - 1].size());
          Position joinPos(pos.line - 1, 0);
          VimEditUtils::joinLines(lines, joinPos, false);
          pos = Position(pos.line - 1, prevLen);
        } else {
          // Delete char before cursor
          Position beforePos(pos.line, pos.col - 1);
          Range r(beforePos, beforePos, false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        return;

      case hash("<Del>"):
        {
          int len = static_cast<int>(lines[pos.line].size());
          if (pos.col >= len && pos.line + 1 >= static_cast<int>(lines.size())) {
            throw runtime_error("<Del> at end of buffer has no effect");
          }
          if (pos.col >= len) {
            // At end of line - join with next line
            VimEditUtils::joinLines(lines, pos, false);
          } else {
            // Delete char at cursor
            Range r(pos, pos, false, true);
            VimEditUtils::deleteRange(lines, r, pos);
          }
        }
        return;

      case hash("<CR>"):
        VimEditUtils::insertText(lines, pos, "\n");
        return;

      case hash("<C-u>"):
        if (pos.col == 0) {
          throw runtime_error("<C-u> at start of line has no effect");
        }
        {
          Range r(Position(pos.line, 0), Position(pos.line, pos.col - 1), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        return;

      case hash("<C-w>"):
        if (pos.col == 0) {
          throw runtime_error("<C-w> at start of line has no effect");
        }
        {
          int col = pos.col - 1;
          const string& ln = lines[pos.line];
          // Skip whitespace backwards
          while (col > 0 && VimUtils::isBlank(ln[col])) col--;
          // Delete word chars backwards
          if (VimUtils::isSmallWordChar(ln[col])) {
            while (col > 0 && VimUtils::isSmallWordChar(ln[col - 1])) col--;
          } else if (!VimUtils::isBlank(ln[col])) {
            // Non-word, non-blank: delete punctuation sequence
            while (col > 0 && !VimUtils::isSmallWordChar(ln[col - 1]) &&
                   !VimUtils::isBlank(ln[col - 1])) col--;
          }
          Range r(Position(pos.line, col), Position(pos.line, pos.col - 1), false, true);
          VimEditUtils::deleteRange(lines, r, pos);
        }
        return;

      case hash("<Left>"):
        if (pos.col == 0) {
          throw runtime_error("<Left> at start of line has no effect");
        }
        pos.col--;
        return;

      case hash("<Right>"):
        if (pos.col >= static_cast<int>(lines[pos.line].size())) {
          throw runtime_error("<Right> at end of line has no effect");
        }
        pos.col++;
        return;

      case hash("<Up>"):
        if (pos.line == 0) {
          throw runtime_error("<Up> at first line has no effect");
        }
        pos.line--;
        pos.col = min(pos.col, static_cast<int>(lines[pos.line].size()));
        return;

      case hash("<Down>"):
        if (pos.line + 1 >= static_cast<int>(lines.size())) {
          throw runtime_error("<Down> at last line has no effect");
        }
        pos.line++;
        pos.col = min(pos.col, static_cast<int>(lines[pos.line].size()));
        return;
    }
  }

  throw runtime_error("Unknown edit: " + string(e));
}

} // namespace Edit
