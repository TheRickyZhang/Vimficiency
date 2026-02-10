#include "Edit.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimOptions.h"
#include "VimCore/VimTextObjectsDeprecated.h"
#include "VimCore/VimCore.h"
#include "Utils/Debug.h"
#include "Utils/Lines.h"

#include <cassert>
#include <stdexcept>
#include <algorithm>

using namespace std;

namespace Edit {

// Compile-time string hash for switch statements.
// Uses M=131 (prime > 126, total chars supported) to guarantee collision-free hashing for all strings.
// Math: hash(s) = s[0]*M^(n-1) + s[1]*M^(n-2) + ... + s[n-1]
// This is bijective (unique hash per string) until 64-bit overflow at ~10 chars.
constexpr size_t hash(string_view s) {
  assert(s.size() <= 10 && "hash() only collision-free for strings <= 10 chars");
  size_t h = 0;
  for (char c : s) h = h * 131 + static_cast<unsigned char>(c);
  return h;
}

inline int clampedToLastChar(const string& line, int col) {
  return line.empty() ? 0 : min(col, (int)line.size() - 1);
}

// -----------------------------------------------------------------------------
// Word motion deletion helpers
//
// Special case from Vim docs: "For dw/dW on the last word of a line, the
// newline is not included." This ONLY applies when:
//   1. count == 1 (single word deletion)
//   2. Motion crosses to next line
//   3. Current line is non-empty (empty lines are "words" that include newline)
// -----------------------------------------------------------------------------

// Check if the "don't cross lines" special case applies
static bool shouldStopAtEndOfLine(int count, const Position& initialPos,
                                   const Position& goalPos, const Lines& lines) {
  return count == 1
      && goalPos.line > initialPos.line
      && !lines[initialPos.line].empty();
}

// Delete from pos to end of current line (inclusive of last char, but not newline)
static void deleteToEndOfLine(Lines& lines, Position& pos) {
  int lastCol = static_cast<int>(lines[pos.line].size()) - 1;
  if (lastCol >= pos.col) {
    Range r(pos, Position(pos.line, lastCol));
    VimCore::deleteRange(lines, r, pos);
  }
}

// Check if position is "past end" (col == line.size())
// Word motions return this when they reach EOF, distinguishing:
// - landed on valid word boundary (col < line.size())
// - wanted to go further but hit EOF (col == line.size())
static bool isPastEndPosition(const Lines& lines, const Position& pos) {
  // Lines invariant: buffer always has at least one line
  if (pos.line >= static_cast<int>(lines.size())) return false;
  return pos.col == static_cast<int>(lines[pos.line].size());
}

// -----------------------------------------------------------------------------
// Shared d/c operator helpers - compute range and delete with given mode
// These ensure d and c operators have identical range computation.
// -----------------------------------------------------------------------------

// de/dE/ce/cE: delete to end of word (inclusive motion)
static void deleteToWordEnd(Lines& lines, Position& pos, int count, bool big, Mode mode) {
  Position goalPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionE(goalPos, lines, big);

  if (isPastEndPosition(lines, goalPos)) {
    // e motion wanted to go past EOF - delete to last char inclusive
    goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
  }

  // Inclusive delete: goalPos >= pos means we have something to delete
  // This handles the case where motion didn't move (single char at EOF)
  if (goalPos.line > pos.line || goalPos.col >= pos.col) {
    Range r(pos, goalPos);
    VimCore::deleteRange(lines, r, pos, mode);
  }
}

// dw/dW: delete to next word start (exclusive motion, special line-crossing rule)
static void deleteToNextWord(Lines& lines, Position& pos, int count, bool big, Mode mode) {
  Position goalPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionW(goalPos, lines, big);

  if (shouldStopAtEndOfLine(count, pos, goalPos, lines)) {
    // Special case: single dw on non-empty line crossing to next line
    deleteToEndOfLine(lines, pos);
  } else if (isPastEndPosition(lines, goalPos)) {
    // Motion wanted to go past EOF - delete to last char inclusive
    goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
    if (goalPos.line > pos.line || goalPos.col >= pos.col) {
      Range r(pos, goalPos);
      VimCore::deleteRange(lines, r, pos, mode);
    }
  } else if (goalPos.line > pos.line || goalPos.col > pos.col) {
    // Normal exclusive delete - compute inclusive end (position before goalPos)
    Position inclusiveEnd;
    if (goalPos.col > 0) {
      inclusiveEnd = Position(goalPos.line, goalPos.col - 1);
    } else {
      // goalPos at col 0 of new line - delete up to end of previous line
      inclusiveEnd = Position(goalPos.line - 1, static_cast<int>(lines[goalPos.line - 1].size()) - 1);
    }
    Range r(pos, inclusiveEnd);
    VimCore::deleteRange(lines, r, pos, mode);
  }
}

// db/dB: delete backward to word start (exclusive motion)
static void deleteBackToWordStart(Lines& lines, Position& pos, int count, bool big, Mode mode) {
  Position initialPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionB(initialPos, lines, big);

  if (initialPos < pos) {
    // b is exclusive motion: delete from where b lands to just BEFORE cursor
    if (pos.col == 0 && initialPos.line < pos.line) {
      // Delete to end of previous line (inclusive), keeping current line
      int prevLine = pos.line - 1;
      int lastCol = lines[prevLine].empty() ? 0 : static_cast<int>(lines[prevLine].size()) - 1;
      Range r(initialPos, Position(prevLine, lastCol));
      VimCore::deleteRange(lines, r, pos, mode);
    } else {
      Range r(initialPos, Position(pos.line, pos.col - 1));
      VimCore::deleteRange(lines, r, pos, mode);
    }
  }
}

// dge/dgE: delete backward to previous word end (inclusive motion)
static void deleteBackToWordEnd(Lines& lines, Position& pos, int count, bool big, Mode mode) {
  Position initialPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionGe(initialPos, lines, big);

  if (initialPos < pos) {
    // ge is an INCLUSIVE backward motion - include current position
    Range r(initialPos, pos);
    VimCore::deleteRange(lines, r, pos, mode);
  }
}

// -----------------------------------------------------------------------------
// Operator + Range operations (called directly, not through applyEdit)
// -----------------------------------------------------------------------------

void deleteRange(Lines& lines, Position& pos, Mode mode, const Range& range) {
  assert(mode == Mode::Normal);
  VimCore::deleteRange(lines, range, pos);
}

void changeRange(Lines& lines, Position& pos, Mode& mode, const Range& range) {
  assert(mode == Mode::Normal);
  VimCore::deleteRange(lines, range, pos);
  mode = Mode::Insert;
}

void yankRange(Lines& lines, Position& pos, Mode mode, const Range& range) {
  assert(mode == Mode::Normal);
  Range r = range;
  r.normalize();
  pos = r.first;
}

// -----------------------------------------------------------------------------
// Insert mode text insertion (called directly for typed characters)
// -----------------------------------------------------------------------------

void insertText(Lines& lines, Position& pos, Mode mode, string_view text) {
  assert(mode == Mode::Insert);
  VimCore::insertText(lines, pos, text);
}

// -----------------------------------------------------------------------------
// Application
// -----------------------------------------------------------------------------

// Error on actions that don't do anything - these should be pruned by search logic.
// Note: something like 3dw on a line may do nothing on 2nd/3rd dw, but since
// we can't prune that easily without doing equivalent work as the action, it's fine.
// TODO: we can apply the same technique to Motions as well
void applyEdit(Lines& lines, Position& pos, Mode& mode, const ParsedEdit& edit) {
  // Lines invariant: buffer always has at least one line (minimum: {""})
  assert(!lines.empty());

  string_view e = edit.edit;
  int count = edit.effectiveCount();
  size_t h = hash(e);

  string& line = lines[pos.line];
  int n = static_cast<int>(lines.size());
  int m = static_cast<int>(line.size());

  // Handle visual mode sequences first (before empty line check)
  // These are parsed as complete tokens like "vjd", "v}d", "vwhjd"
  if (mode == Mode::Normal && e.size() >= 2 && e[0] == 'v') {
    char op = e.back();  // 'd' or 'c'
    if (op != 'd' && op != 'c') {
      throw runtime_error("Visual mode sequence must end with 'd' or 'c': " + string(e));
    }

    // Extract motion part (between 'v' and operator)
    string motionSeq(e.substr(1, e.size() - 2));

    // Record anchor position
    Position anchor = pos;

    // Apply motions to get end position
    for (const auto& motion : parseEdits(motionSeq)) {
      applyEdit(lines, pos, mode, motion);
    }

    // Compute range (visual mode is inclusive of both endpoints)
    Range r(anchor, pos);
    r.normalize();

    // Apply operator
    if (op == 'd') {
      VimCore::deleteRange(lines, r, pos);
    } else {  // op == 'c'
      VimCore::deleteRange(lines, r, pos, Mode::Insert);
      mode = Mode::Insert;
    }
    return;
  }

  // Empty line: only switch to insert mode, vertical motion, navigation, and word motions
  // (empty line is considered a "word" for dw/dW purposes)
  if (line.empty() && mode == Mode::Normal) {
    switch (h) {
      case hash("i"): case hash("a"): case hash("I"): case hash("A"):
      // Line operations (valid)
      case hash("o"): case hash("O"): case hash("dd"): case hash("cc"): case hash("S"):
      case hash("J"): case hash("gJ"):

      // Word motions
      case hash("w"): case hash("W"): case hash("b"): case hash("B"):
      case hash("e"): case hash("E"): case hash("ge"): case hash("gE"):
      case hash("dw"): case hash("dW"): case hash("db"): case hash("dB"):
      case hash("de"): case hash("dE"): case hash("dge"): case hash("dgE"):
      case hash("d}"): case hash("d{"): case hash("d)"): case hash("d("):
      case hash("cw"): case hash("cW"): case hash("cb"): case hash("cB"):
      case hash("ce"): case hash("cE"): case hash("cge"): case hash("cgE"):
      case hash("c}"): case hash("c{"): case hash("c)"): case hash("c("):
      case hash("0"): case hash("^"): case hash("$"):
      // Navigation (for EditOptimizer line traversal)
      case hash("j"): case hash("k"): case hash("h"): case hash("l"):
      // Paragraph/sentence motions
      case hash("}"): case hash("{"): case hash(")"): case hash("("):
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
      pos.setCol(pos.col + count - 1);
      return;
    }

    switch (hash(e)) {
      case hash("x"):
        if (pos.col + count > m) {
          throw runtime_error("x requires " + to_string(count) + " chars");
        }
        line.erase(pos.col, count);
        pos.setCol(clampedToLastChar(line, pos.col));
        return;

      case hash("X"):
        if (count > pos.col) {
          throw runtime_error("X requires " + to_string(count) + " chars before cursor");
        }
        line.erase(pos.col - count, count);
        pos.setCol(pos.col - count);
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
        pos.setCol(pos.col + count - 1);
        return;

      case hash("J"):
        if (pos.line + count >= n) {
          throw runtime_error("J requires " + to_string(count) + " lines below");
        }
        for (int i = 0; i < count; i++) VimCore::joinLines(lines, pos, true);
        return;

      case hash("gJ"):
        if (pos.line + count >= n) {
          throw runtime_error("gJ requires " + to_string(count) + " lines below");
        }
        for (int i = 0; i < count; i++) VimCore::joinLines(lines, pos, false);
        return;

      case hash("dd"):
        if (pos.line + count > n) {
          throw runtime_error("dd requires " + to_string(count) + " lines");
        }
        lines.erase(lines.begin() + pos.line, lines.begin() + pos.line + count);
        // Maintain invariant: buffer always has at least one line
        if (lines.empty()) {
          lines.push_back("");
        }
        pos.line = min(pos.line, static_cast<int>(lines.size()) - 1);
        if (VimOptions::startOfLine()) {
          // Vim default: go to first non-blank, update targetCol
          pos.setCol(VimCore::firstNonBlankColInLineStr(lines[pos.line]));
        } else {
          // Neovim default: dd resets targetCol to the clamped column
          if (lines[pos.line].empty()) {
            pos.setCol(0);
          } else {
            pos.setCol(min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
          }
        }
        return;

      case hash("cc"): case hash("S"):
        line.clear();
        pos.setCol(0);
        mode = Mode::Insert;
        return;

      case hash("o"):
        VimCore::openLineBelow(lines, pos);
        mode = Mode::Insert;
        return;

      case hash("O"):
        VimCore::openLineAbove(lines, pos);
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
        pos.setCol(VimCore::firstNonBlankColInLineStr(lines[pos.line]));
        mode = Mode::Insert;
        return;

      case hash("a"):
        pos.setCol(pos.col + 1);
        mode = Mode::Insert;
        return;

      case hash("A"):
        pos.setCol(m);
        mode = Mode::Insert;
        return;

      // --- Word deletion motions (d + motion) ---
      case hash("dw"):
      case hash("dW"):
        deleteToNextWord(lines, pos, count, e == "dW", Mode::Normal);
        return;

      case hash("de"):
      case hash("dE"):
        deleteToWordEnd(lines, pos, count, e == "dE", Mode::Normal);
        return;

      case hash("db"):
      case hash("dB"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        deleteBackToWordStart(lines, pos, count, e == "dB", Mode::Normal);
        return;

      case hash("dge"):
      case hash("dgE"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        deleteBackToWordEnd(lines, pos, count, e == "dgE", Mode::Normal);
        return;

      case hash("d0"):
        if (count > 1) {
          debug("d0: count", count, "ignored (0 motion doesn't use count)");
        }
        if (pos.col == 0) {
          throw runtime_error("d0 at column 0 has no effect");
        }
        {
          Range r(Position(pos.line, 0), Position(pos.line, pos.col - 1));
          VimCore::deleteRange(lines, r, pos);
        }
        return;

      case hash("d^"):
        if (count > 1) {
          debug("d^: count", count, "ignored (^ motion doesn't use count)");
        }
        {
          int firstNonBlank = VimCore::firstNonBlankColInLineStr(lines[pos.line]);
          if (firstNonBlank >= pos.col) {
            throw runtime_error("d^ at or before first non-blank has no effect");
          }
          Range r(Position(pos.line, firstNonBlank), Position(pos.line, pos.col - 1));
          VimCore::deleteRange(lines, r, pos);
        }
        return;

      // --- Change motions (c + motion) ---
      // Vim special case: cw/cW on a word changes to end of CURRENT word only
      // (doesn't include trailing whitespace, doesn't cross to next word)
      // Only when on whitespace does it use w motion semantics
      case hash("cw"):
      case hash("cW"):
        {
          if (line.empty()) {
            // Empty line: just enter insert mode, nothing to change
            mode = Mode::Insert;
            return;
          }
          bool big = (e == "cW");
          unsigned char c = static_cast<unsigned char>(line[pos.col]);
          bool onWord = big ? VimCore::isBigWordChar(c) : VimCore::isSmallWordChar(c);

          if (onWord) {
            // On a word: find end of CURRENT word (don't use e motion which goes to next word)
            // Stay on same line, find last char of current word type
            auto isWordChar = [big](unsigned char ch) {
              return big ? VimCore::isBigWordChar(ch) : VimCore::isSmallWordChar(ch);
            };
            int endCol = pos.col;
            int lineLen = static_cast<int>(line.size());
            while (endCol + 1 < lineLen && isWordChar(static_cast<unsigned char>(line[endCol + 1]))) {
              endCol++;
            }
            // Delete from current position to end of current word (inclusive)
            // Use Insert mode for positioning since we're about to enter Insert mode
            Range r(pos, Position(pos.line, endCol));
            VimCore::deleteRange(lines, r, pos, Mode::Insert);
          } else {
            // On whitespace: use w/W motion (change to start of next word)
            Position goalPos = pos;
            for (int i = 0; i < count; i++) VimCore::motionW(goalPos, lines, big);
            if (shouldStopAtEndOfLine(count, pos, goalPos, lines)) {
              deleteToEndOfLine(lines, pos);
            } else if (isPastEndPosition(lines, goalPos)) {
              // w motion wanted to go past EOF - delete to last char inclusive
              goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
              if (goalPos.line > pos.line || goalPos.col >= pos.col) {
                Range r(pos, goalPos);
                VimCore::deleteRange(lines, r, pos);
              }
            } else if (goalPos.line > pos.line || goalPos.col > pos.col) {
              // Normal case: exclusive delete - compute inclusive end (position before goalPos)
              Position inclusiveEnd;
              if (goalPos.col > 0) {
                inclusiveEnd = Position(goalPos.line, goalPos.col - 1);
              } else {
                // goalPos at col 0 of new line - delete up to end of previous line
                inclusiveEnd = Position(goalPos.line - 1, static_cast<int>(lines[goalPos.line - 1].size()) - 1);
              }
              Range r(pos, inclusiveEnd);
              VimCore::deleteRange(lines, r, pos);
            }
          }
          mode = Mode::Insert;
        }
        return;

      case hash("ce"):
      case hash("cE"):
        deleteToWordEnd(lines, pos, count, e == "cE", Mode::Insert);
        mode = Mode::Insert;
        return;

      case hash("cb"):
      case hash("cB"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        {
          bool big = (e == "cB");
          Position initialPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionB(initialPos, lines, big);
          if (initialPos < pos) {
            // For cb/cB, don't delete across newline boundaries (same as cw/cW)
            // If motion crossed to previous line and we're at col 0, only delete
            // to end of the line where b landed
            Position goalPos;
            if (initialPos.line < pos.line && pos.col == 0) {
              // Delete to end of the line where b landed
              int lastCol = static_cast<int>(lines[initialPos.line].size()) - 1;
              goalPos = Position(initialPos.line, lastCol >= 0 ? lastCol : 0);
            } else {
              goalPos = Position(pos.line, pos.col > 0 ? pos.col - 1 : 0);
            }
            if (initialPos <= goalPos) {
              Range r(initialPos, goalPos);
              VimCore::deleteRange(lines, r, pos);
            }
          }
          mode = Mode::Insert;
        }
        return;

      case hash("cge"):
      case hash("cgE"):
        if (pos.line == 0 && pos.col == 0) {
          throw runtime_error(string(e) + " at start of buffer has no effect");
        }
        deleteBackToWordEnd(lines, pos, count, e == "cgE", Mode::Insert);
        mode = Mode::Insert;
        return;

      case hash("c0"):
        if (count > 1) {
          debug("c0: count", count, "ignored (0 motion doesn't use count)");
        }
        if (pos.col == 0) {
          throw runtime_error("c0 at column 0 has no effect");
        }
        {
          Range r(Position(pos.line, 0), Position(pos.line, pos.col - 1));
          VimCore::deleteRange(lines, r, pos);
        }
        mode = Mode::Insert;
        return;

      case hash("c^"):
        if (count > 1) {
          debug("c^: count", count, "ignored (^ motion doesn't use count)");
        }
        {
          int firstNonBlank = VimCore::firstNonBlankColInLineStr(lines[pos.line]);
          if (firstNonBlank >= pos.col) {
            throw runtime_error("c^ at or before first non-blank has no effect");
          }
          Range r(Position(pos.line, firstNonBlank), Position(pos.line, pos.col - 1));
          VimCore::deleteRange(lines, r, pos);
        }
        mode = Mode::Insert;
        return;

      case hash("C"):
      case hash("c$"):
        if (pos.line + count > n) {
          throw runtime_error("c$ requires " + to_string(count) + " lines but only " + to_string(n - pos.line) + " available");
        }
        {
          int endLine = pos.line + count - 1;
          int endCol = lines[endLine].empty() ? 0 : static_cast<int>(lines[endLine].size()) - 1;
          Range r(pos, Position(endLine, endCol));
          VimCore::deleteRange(lines, r, pos);
          mode = Mode::Insert;
        }
        return;

      case hash("D"):
      case hash("d$"):
        if (pos.line + count > n) {
          throw runtime_error("d$ requires " + to_string(count) + " lines but only " + to_string(n - pos.line) + " available");
        }
        {
          int endLine = pos.line + count - 1;
          int endCol = lines[endLine].empty() ? 0 : static_cast<int>(lines[endLine].size()) - 1;
          Range r(pos, Position(endLine, endCol));
          VimCore::deleteRange(lines, r, pos);
        }
        return;

      // --- Paragraph motions ---
      case hash("d}"):
      case hash("c}"):
        {
          Position goalPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionParagraphNext(goalPos, lines);
          // } is exclusive. Two special cases match Vim's operator behavior:
          //
          // 1. Col-0 linewise: when } lands at col 0 on a line past the start,
          //    Vim converts the exclusive motion to linewise (deletes whole lines
          //    from pos.line to goalPos.line - 1).
          //
          // 2. EOF inclusive: when } reaches the last non-blank line,
          //    motionParagraphNext lands ON the last char (not past it).
          //    The position is already inclusive — don't apply exclusive backup.
          //    Also handle goalPos == pos (cursor already at last char).
          bool atCol0 = goalPos.col == 0 && goalPos.line > pos.line && pos.col == 0;
          bool atEof = goalPos.line == lines.lastLine()
                    && !VimCore::isBlankLineStr(lines[goalPos.line]);
          if (atCol0) {
            VimCore::deleteRangeLinewise(lines, LineRange(pos.line, goalPos.line - 1), pos);
          } else if (atEof ? (goalPos >= pos) : (goalPos > pos)) {
            Position inclusiveEnd = atEof ? goalPos : lines.getPrevPos(goalPos);
            if (inclusiveEnd != POSITION_OUTSIDE_BOUNDARY && inclusiveEnd >= pos) {
              Range r(pos, inclusiveEnd);
              VimCore::deleteRange(lines, r, pos, e[0] == 'c' ? Mode::Insert : Mode::Normal);
            }
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      case hash("d{"):
      case hash("c{"):
        {
          Position initialPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionParagraphPrev(initialPos, lines);
          // d{ is exclusive: delete from motion endpoint to just before cursor
          if (initialPos < pos) {
            Position goalPos = pos.col > 0 ? Position(pos.line, pos.col - 1)
                            : (pos.line > 0 ? Position(pos.line - 1, lines[pos.line - 1].lastCol()) : pos);
            if (initialPos <= goalPos) {
              Range r(initialPos, goalPos);
              VimCore::deleteRange(lines, r, pos, e[0] == 'c' ? Mode::Insert : Mode::Normal);
            }
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      // --- Sentence motions ---
      case hash("d)"):
      case hash("c)"):
        {
          Position goalPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionSentenceNext(goalPos, lines);
          // d) is exclusive: delete up to but not including the sentence start
          if (goalPos > pos) {
            Position inclusiveEnd = lines.getPrevPos(goalPos);
            if (inclusiveEnd != POSITION_OUTSIDE_BOUNDARY && inclusiveEnd >= pos) {
              Range r(pos, inclusiveEnd);
              VimCore::deleteRange(lines, r, pos, e[0] == 'c' ? Mode::Insert : Mode::Normal);
            }
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      case hash("d("):
      case hash("c("):
        {
          Position initialPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionSentencePrev(initialPos, lines);
          // d( is exclusive: delete from motion endpoint to just before cursor
          if (initialPos < pos) {
            Position goalPos = pos.col > 0 ? Position(pos.line, pos.col - 1)
                            : (pos.line > 0 ? Position(pos.line - 1, lines[pos.line - 1].lastCol()) : pos);
            if (initialPos <= goalPos) {
              Range r(initialPos, goalPos);
              VimCore::deleteRange(lines, r, pos, e[0] == 'c' ? Mode::Insert : Mode::Normal);
            }
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      // --- Navigation motions (for EditOptimizer) ---
      case hash("j"):
        if (pos.line + count >= n) {
          throw runtime_error("j requires " + to_string(count) + " lines below");
        }
        pos.line += count;
        pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
        return;

      case hash("k"):
        if (pos.line < count) {
          throw runtime_error("k requires " + to_string(count) + " lines above");
        }
        pos.line -= count;
        pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
        return;

      case hash("h"):
        if (pos.col < count) {
          throw runtime_error("h requires " + to_string(count) + " chars left");
        }
        pos.setCol(pos.col - count);
        return;

      case hash("l"):
        if (pos.col + count >= m) {
          throw runtime_error("l requires " + to_string(count) + " chars right");
        }
        pos.setCol(pos.col + count);
        return;

      case hash("w"):
        for (int i = 0; i < count; i++) VimCore::motionW(pos, lines, false);
        return;

      case hash("W"):
        for (int i = 0; i < count; i++) VimCore::motionW(pos, lines, true);
        return;

      case hash("b"):
        for (int i = 0; i < count; i++) VimCore::motionB(pos, lines, false);
        return;

      case hash("B"):
        for (int i = 0; i < count; i++) VimCore::motionB(pos, lines, true);
        return;

      case hash("e"):
        for (int i = 0; i < count; i++) VimCore::motionE(pos, lines, false);
        return;

      case hash("E"):
        for (int i = 0; i < count; i++) VimCore::motionE(pos, lines, true);
        return;

      case hash("ge"):
        for (int i = 0; i < count; i++) VimCore::motionGe(pos, lines, false);
        return;

      case hash("gE"):
        for (int i = 0; i < count; i++) VimCore::motionGe(pos, lines, true);
        return;

      case hash("0"):
        pos.setCol(0);
        return;

      case hash("^"):
        pos.setCol(VimCore::firstNonBlankColInLineStr(line));
        return;

      case hash("$"):
        pos.setCol(m > 0 ? m - 1 : 0);
        return;

      case hash("}"):
        for (int i = 0; i < count; i++) VimCore::motionParagraphNext(pos, lines);
        return;

      case hash("{"):
        for (int i = 0; i < count; i++) VimCore::motionParagraphPrev(pos, lines);
        return;

      case hash(")"):
        for (int i = 0; i < count; i++) VimCore::motionSentenceNext(pos, lines);
        return;

      case hash("("):
        for (int i = 0; i < count; i++) VimCore::motionSentencePrev(pos, lines);
        return;

    }

    // --- Text object operations: operator + modifier + object ---
    // Pattern: [d|c] + [i|a] + [w|W|"|'|(|)|{|}|[|]|<|>|b|B]
    if (e.size() == 3 && (e[0] == 'd' || e[0] == 'c') && (e[1] == 'i' || e[1] == 'a')) {
      char op = e[0];
      bool inner = (e[1] == 'i');
      char obj = e[2];

      Range r(pos, pos);  // Default: empty range

      // Word objects - use VimCore
      if (obj == 'w' || obj == 'W') {
        Lines linesWrapper(lines.begin(), lines.end());
        bool bigWord = (obj == 'W');
        r = VimCore::textObject(pos, linesWrapper, inner, bigWord);
      }
      // Quote objects
      else if (obj == '"' || obj == '\'' || obj == '`') {
        r = inner ? VimTextObjectsDeprecated::innerQuote(lines, pos, obj)
                  : VimTextObjectsDeprecated::aroundQuote(lines, pos, obj);
      }
      // Bracket objects - handle both opening and closing chars
      else if (obj == '(' || obj == ')' || obj == 'b') {
        r = inner ? VimTextObjectsDeprecated::innerBracket(lines, pos, '(', ')')
                  : VimTextObjectsDeprecated::aroundBracket(lines, pos, '(', ')');
      } else if (obj == '{' || obj == '}' || obj == 'B') {
        r = inner ? VimTextObjectsDeprecated::innerBracket(lines, pos, '{', '}')
                  : VimTextObjectsDeprecated::aroundBracket(lines, pos, '{', '}');
      } else if (obj == '[' || obj == ']') {
        r = inner ? VimTextObjectsDeprecated::innerBracket(lines, pos, '[', ']')
                  : VimTextObjectsDeprecated::aroundBracket(lines, pos, '[', ']');
      } else if (obj == '<' || obj == '>') {
        r = inner ? VimTextObjectsDeprecated::innerBracket(lines, pos, '<', '>')
                  : VimTextObjectsDeprecated::aroundBracket(lines, pos, '<', '>');
      } else {
        throw runtime_error("Unknown text object: " + string(1, obj));
      }

      // Apply operator to range (all ranges are now inclusive)
      if (r.isValid()) {
        if (op == 'd') {
          VimCore::deleteRange(lines, r, pos);
        } else {  // op == 'c'
          VimCore::deleteRange(lines, r, pos, Mode::Insert);
          mode = Mode::Insert;
        }
      } else if (op == 'c') {
        // Invalid/empty range but still enter insert mode for 'c'
        mode = Mode::Insert;
      }
      return;
    }
  }

  if (mode == Mode::Insert) {
    switch (hash(e)) {
      case hash("<Esc>"):
        if (pos.col > 0) pos.setCol(pos.col - 1);
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
          VimCore::joinLines(lines, joinPos, false);
          pos = Position(pos.line - 1, prevLen);
        } else {
          // Delete char before cursor
          Position beforePos(pos.line, pos.col - 1);
          Range r(beforePos, beforePos);
          VimCore::deleteRange(lines, r, pos, Mode::Insert);
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
            VimCore::joinLines(lines, pos, false);
          } else {
            // Delete char at cursor
            Range r(pos, pos);
            VimCore::deleteRange(lines, r, pos, Mode::Insert);
          }
        }
        return;

      case hash("<CR>"):
        VimCore::insertText(lines, pos, "\n");
        return;

      case hash("<C-u>"):
        if (pos.col == 0) {
          throw runtime_error("<C-u> at start of line has no effect");
        }
        {
          Range r(Position(pos.line, 0), Position(pos.line, pos.col - 1));
          VimCore::deleteRange(lines, r, pos, Mode::Insert);
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
          while (col > 0 && VimCore::isBlank(ln[col])) col--;
          // Delete word chars backwards
          if (VimCore::isSmallWordChar(ln[col])) {
            while (col > 0 && VimCore::isSmallWordChar(ln[col - 1])) col--;
          } else if (!VimCore::isBlank(ln[col])) {
            // Non-word, non-blank: delete punctuation sequence
            while (col > 0 && !VimCore::isSmallWordChar(ln[col - 1]) &&
                   !VimCore::isBlank(ln[col - 1])) col--;
          }
          Range r(Position(pos.line, col), Position(pos.line, pos.col - 1));
          VimCore::deleteRange(lines, r, pos, Mode::Insert);
        }
        return;

      case hash("<Left>"):
        if (pos.col == 0) {
          throw runtime_error("<Left> at start of line has no effect");
        }
        pos.setCol(pos.col - 1);
        return;

      case hash("<Right>"):
        if (pos.col >= static_cast<int>(lines[pos.line].size())) {
          throw runtime_error("<Right> at end of line has no effect");
        }
        pos.setCol(pos.col + 1);
        return;

      case hash("<Up>"):
        if (pos.line == 0) {
          throw runtime_error("<Up> at first line has no effect");
        }
        pos.line--;
        // Insert mode <Up>/<Down> use sticky column behavior like j/k
        pos.clampColPreservingTarget(min(pos.targetCol, static_cast<int>(lines[pos.line].size())));
        return;

      case hash("<Down>"):
        if (pos.line + 1 >= static_cast<int>(lines.size())) {
          throw runtime_error("<Down> at last line has no effect");
        }
        pos.line++;
        // Insert mode <Up>/<Down> use sticky column behavior like j/k
        pos.clampColPreservingTarget(min(pos.targetCol, static_cast<int>(lines[pos.line].size())));
        return;
    }
  }

  throw runtime_error("Unknown edit: " + string(e));
}

// =============================================================================
// Edit Sequence Parsing
// =============================================================================

vector<ParsedEdit> parseEdits(string_view seq) {
  string_view sv(seq);
  vector<ParsedEdit> result;
  size_t i = 0;

  while (i < sv.size()) {
    char c = sv[i];

    // Parse optional count prefix
    int cnt = 0;
    if (isdigit(c) && c != '0') {
      size_t start = i;
      while (i < sv.size() && isdigit(sv[i])) {
        i++;
      }
      for (size_t j = start; j < i; j++) {
        cnt = cnt * 10 + (sv[j] - '0');
      }
      if (i >= sv.size()) break;
      c = sv[i];
    }

    // Handle <...> notation for special keys
    if (c == '<') {
      size_t close = sv.find('>', i);
      if (close != string_view::npos) {
        string_view special = sv.substr(i, close - i + 1);
        result.push_back(ParsedEdit{special, cnt});
        i = close + 1;
        continue;
      }
      throw runtime_error("Malformed special key at: " + string(sv.substr(i)));
    }

    // Handle r{char} - replace with specific character
    if (c == 'r' && i + 1 < sv.size()) {
      result.push_back(ParsedEdit{sv.substr(i, 2), cnt});
      i += 2;
      continue;
    }

    // Operators that take motions: d, c
    if (c == 'd' || c == 'c') {
      // Check for doubled operator (dd, cc)
      if (i + 1 < sv.size() && sv[i + 1] == c) {
        result.push_back(ParsedEdit{sv.substr(i, 2), cnt});
        i += 2;
        continue;
      }

      // Check for text objects: d/c + i/a + object
      if (i + 2 < sv.size() && (sv[i + 1] == 'i' || sv[i + 1] == 'a')) {
        result.push_back(ParsedEdit{sv.substr(i, 3), cnt});
        i += 3;
        continue;
      }

      // Check for operator + motion (dw, de, db, dge, etc.)
      if (i + 1 < sv.size()) {
        char next = sv[i + 1];
        // g-prefix motions: dge, dgE
        if (next == 'g' && i + 2 < sv.size()) {
          result.push_back(ParsedEdit{sv.substr(i, 3), cnt});
          i += 3;
          continue;
        }
        // Simple motions: dw, de, db, d0, d$, etc.
        result.push_back(ParsedEdit{sv.substr(i, 2), cnt});
        i += 2;
        continue;
      }

      // Fallback: just the operator character
      result.push_back(ParsedEdit{sv.substr(i, 1), cnt});
      i++;
      continue;
    }

    // g-prefix commands: ge, gE, gJ
    if (c == 'g' && i + 1 < sv.size()) {
      char next = sv[i + 1];
      if (next == 'e' || next == 'E' || next == 'J') {
        result.push_back(ParsedEdit{sv.substr(i, 2), cnt});
        i += 2;
        continue;
      }
    }

    // Visual mode: v + motion(s) + operator (d or c)
    // Parse entire visual sequence as single token
    if (c == 'v') {
      size_t start = i;
      i++;  // Skip 'v'
      // Find the ending operator (d or c)
      while (i < sv.size() && sv[i] != 'd' && sv[i] != 'c') {
        i++;
      }
      if (i < sv.size()) {
        i++;  // Include the operator
      }
      result.push_back(ParsedEdit{sv.substr(start, i - start), cnt});
      continue;
    }

    // Single character (for insert mode typed characters, navigation, etc.)
    result.push_back(ParsedEdit{sv.substr(i, 1), cnt});
    i++;
  }

  return result;
}

} // namespace Edit
