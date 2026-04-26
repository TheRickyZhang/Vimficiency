#include "EditInterpreter.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimOptions.h"
#include "VimCore/VimTextObjectsLegacy.h"
#include "VimCore/VimCore.h"
#include "Utils/Debug.h"
#include "Types/Lines.h"

#include <algorithm>
#include <cassert>
#include <limits>

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

static void adjustCursorAfterBackwardWordDelete(const CharRange& range,
                                                int oldLineCount,
                                                const CursorPos& originalPos,
                                                Lines& lines,
                                                CursorPos& pos,
                                                int firstContentCol = 0) {
  CharRange normalized = range;
  normalized.normalize();

  int cursorContentStart = (originalPos.line == 0) ? firstContentCol : 0;
  if (originalPos.col != cursorContentStart || originalPos.line <= normalized.begin.line) return;
  if (!VimCore::didDeleteRangeRemoveBeginLine(
          normalized, oldLineCount, static_cast<int>(lines.size()))) {
    return;
  }
  if (lines[pos.line].empty()) return;

  pos.setCol(VimCore::firstNonBlankColInLineStr(lines[pos.line]));
}

static bool hasContent(const LineCharRange& range) {
  return range.isValid()
      && (range.beginLine < range.end.line
          || (range.beginLine == range.end.line && range.end.col > 0));
}

void applyDelete(Lines& lines, const CharRange& range, CursorPos& pos, Mode mode) {
  VimCore::deleteRangeAndUpdatePos(lines, range, pos, mode);
}

void applyDelete(Lines& lines, const CharLineRange& range, CursorPos& pos, Mode mode) {
  VimCore::deleteCharLineRangeAndUpdatePos(lines, range, pos, mode);
}

void applyDelete(Lines& lines, const LineCharRange& range, CursorPos& pos, Mode mode) {
  VimCore::deleteLineCharRangeAndUpdatePos(lines, range, pos, mode);
}

CursorPos yankAnchor(const CharRange& range) {
  CharRange normalized = range;
  normalized.normalize();
  return normalized.begin;
}

CursorPos yankAnchor(const CharLineRange& range) {
  return range.begin;
}

CursorPos yankAnchor(const LineCharRange& range) {
  return CursorPos(range.beginLine, 0);
}

template<class RangeT>
void deleteRangeImpl(Lines& lines, CursorPos& pos, Mode mode, const RangeT& range) {
  assert(mode == Mode::Normal);
  applyDelete(lines, range, pos, Mode::Normal);
}

template<class RangeT>
void changeRangeImpl(Lines& lines, CursorPos& pos, Mode& mode, const RangeT& range) {
  assert(mode == Mode::Normal);
  applyDelete(lines, range, pos, Mode::Insert);
  mode = Mode::Insert;
}

template<class RangeT>
void yankRangeImpl(CursorPos& pos, Mode mode, const RangeT& range) {
  assert(mode == Mode::Normal);
  pos = yankAnchor(range);
}

static void applyResolvedDeleteRange(Lines& lines,
                                     CursorPos& pos,
                                     const VimCore::ResolvedDeleteRange& resolved,
                                     Mode mode) {
  switch (resolved.kind) {
    case VimCore::ResolvedDeleteRangeKind::Characterwise:
      if (!resolved.charRange.isEmpty()) {
        applyDelete(lines, resolved.charRange, pos, mode);
      }
      return;
    case VimCore::ResolvedDeleteRangeKind::CharLine:
      applyDelete(lines, resolved.charLineRange, pos, mode);
      return;
    case VimCore::ResolvedDeleteRangeKind::LineChar:
      if (hasContent(resolved.lineCharRange)) {
        applyDelete(lines, resolved.lineCharRange, pos, mode);
      }
      return;
    case VimCore::ResolvedDeleteRangeKind::Linewise:
      VimCore::deleteLineRangeAndUpdatePos(lines, resolved.lineRange, pos);
      return;
  }
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
static bool shouldStopAtEndOfLine(int count, const CursorPos& initialPos,
                                   const CursorPos& goalPos, const Lines& lines) {
  return count == 1
      && goalPos.line > initialPos.line
      && !lines[initialPos.line].empty();
}

// Delete from pos to end of current line (inclusive of last char, but not newline)
static void deleteToEndOfLine(Lines& lines, CursorPos& pos) {
  int lastCol = static_cast<int>(lines[pos.line].size()) - 1;
  if (lastCol >= pos.col) {
    CharRange r(pos, CursorPos(pos.line, lastCol + 1));
    VimCore::deleteRangeAndUpdatePos(lines, r, pos);
  }
}

// Check if position is "past end" (col == line.size())
// Word motions return this when they reach EOF, distinguishing:
// - landed on valid word boundary (col < line.size())
// - wanted to go further but hit EOF (col == line.size())
static bool isPastEndPosition(const Lines& lines, const CursorPos& pos) {
  // Lines invariant: buffer always has at least one line
  if (pos.line >= static_cast<int>(lines.size())) return false;
  return pos.col == static_cast<int>(lines[pos.line].size());
}

static bool inBoundaryRegion(const CursorPos& pos, const Lines& lines,
                             int leftColOffset, int rightColOffset) {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;
  if (pos.line == 0 && pos.col < leftColOffset) return true;
  if (pos.line == lines.lastLine() && rightColOffset > 0 &&
      pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset) {
    return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// Shared d/c operator helpers - compute range and delete with given mode
// These ensure d and c operators have identical range computation.
// -----------------------------------------------------------------------------

// de/dE/ce/cE: delete to end of word (inclusive motion)
static void deleteToWordEnd(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos goalPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionE(goalPos, lines, big);

  if (isPastEndPosition(lines, goalPos)) {
    // e motion wanted to go past EOF - delete to last char inclusive
    goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
  }

  // Inclusive delete: goalPos >= pos means we have something to delete
  // This handles the case where motion didn't move (single char at EOF)
  if (goalPos.line > pos.line || goalPos.col >= pos.col) {
    CharRange r(pos, VimCore::onePastOnSameLine(lines, goalPos));
    VimCore::deleteRangeAndUpdatePos(lines, r, pos, mode);
  }
}

// dw/dW: delete to next word start (exclusive motion, special line-crossing rule)
static void deleteToNextWord(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos goalPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionW(goalPos, lines, big);

  if (shouldStopAtEndOfLine(count, pos, goalPos, lines)) {
    // Special case: single dw on non-empty line crossing to next line
    deleteToEndOfLine(lines, pos);
  } else if (isPastEndPosition(lines, goalPos)) {
    // Motion wanted to go past EOF - delete to last char inclusive
    goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
    if (goalPos.line > pos.line || goalPos.col >= pos.col) {
      CharRange r(pos, VimCore::onePastOnSameLine(lines, goalPos));
      VimCore::deleteRangeAndUpdatePos(lines, r, pos, mode);
    }
  } else if (goalPos.line > pos.line || goalPos.col > pos.col) {
    // Normal exclusive delete - compute one-past-end directly.
    CursorPos end;
    if (goalPos.col > 0) {
      end = goalPos;
    } else {
      // goalPos at col 0 of new line - delete to end of previous line.
      end = CursorPos(goalPos.line - 1, static_cast<int>(lines[goalPos.line - 1].size()));
    }
    CharRange r(pos, end);
    VimCore::deleteRangeAndUpdatePos(lines, r, pos, mode);
  }
}

// db/dB: delete backward to word start (exclusive motion)
static void deleteBackToWordStart(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos initialPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionB(initialPos, lines, big);

  if (initialPos < pos) {
    CharRange r = VimCore::buildBackwardExclusiveCharRange(initialPos, pos, lines);
    int oldLineCount = static_cast<int>(lines.size());
    CursorPos originalPos = pos;
    VimCore::deleteRangeAndUpdatePos(lines, r, pos, mode);
    adjustCursorAfterBackwardWordDelete(r, oldLineCount, originalPos, lines, pos);
  }
}

// dge/dgE: delete backward to previous word end (inclusive motion)
static void deleteBackToWordEnd(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos initialPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionGe(initialPos, lines, big);

  if (initialPos < pos) {
    // ge is an INCLUSIVE backward motion - include current position
    CharRange r(initialPos, VimCore::onePastOnSameLine(lines, pos));
    VimCore::deleteRangeAndUpdatePos(lines, r, pos, mode);
  }
}

// -----------------------------------------------------------------------------
// Operator + CharRange operations (called directly, not through applyEdit)
// -----------------------------------------------------------------------------

void deleteRange(Lines& lines, CursorPos& pos, Mode mode, const CharRange& range) {
  deleteRangeImpl(lines, pos, mode, range);
}

void deleteRange(Lines& lines, CursorPos& pos, Mode mode, const CharLineRange& range) {
  deleteRangeImpl(lines, pos, mode, range);
}

void deleteRange(Lines& lines, CursorPos& pos, Mode mode, const LineCharRange& range) {
  deleteRangeImpl(lines, pos, mode, range);
}

void changeRange(Lines& lines, CursorPos& pos, Mode& mode, const CharRange& range) {
  changeRangeImpl(lines, pos, mode, range);
}

void changeRange(Lines& lines, CursorPos& pos, Mode& mode, const CharLineRange& range) {
  changeRangeImpl(lines, pos, mode, range);
}

void changeRange(Lines& lines, CursorPos& pos, Mode& mode, const LineCharRange& range) {
  changeRangeImpl(lines, pos, mode, range);
}

void yankRange(Lines&, CursorPos& pos, Mode mode, const CharRange& range) {
  yankRangeImpl(pos, mode, range);
}

void yankRange(Lines&, CursorPos& pos, Mode mode, const CharLineRange& range) {
  yankRangeImpl(pos, mode, range);
}

void yankRange(Lines&, CursorPos& pos, Mode mode, const LineCharRange& range) {
  yankRangeImpl(pos, mode, range);
}

// Application
// -----------------------------------------------------------------------------

// Error on actions that don't do anything - these should be pruned by search logic.
// Note: something like 3dw on a line may do nothing on 2nd/3rd dw, but since
// we can't prune that easily without doing equivalent work as the action, it's fine.
// TODO: we can apply the same technique to Motions as well
void applyEdit(Lines& lines, CursorPos& pos, Mode& mode, const ParsedEdit& edit,
               string* lastEditCmd, bool hasLinesBelow,
               int leftColOffset, int rightColOffset,
               bool hasLinesAbove) {
  // Lines invariant: buffer always has at least one line (minimum: {""})
  assert(!lines.empty());

  string_view e = edit.edit;
  int count = edit.effectiveCount();
  size_t h = hash(e);

  // Dot repeat: replay the last buffer-modifying command
  if (mode == Mode::Normal && e == ".") {
    assert(lastEditCmd && !lastEditCmd->empty() && ". with no previous edit to repeat");
    // Parse count from lastEditCmd (e.g., "5de" → edit="de", count=5)
    auto parsed = parseEdits(*lastEditCmd);
    assert(!parsed.empty());
    ParsedEdit repeat = parsed[0];
    // Explicit count on '.' overrides the stored count
    if (edit.hasCount()) {
      repeat = ParsedEdit{repeat.edit, count};
    }
    applyEdit(lines, pos, mode, repeat, nullptr, hasLinesBelow,
              leftColOffset, rightColOffset, hasLinesAbove);  // don't update lastEditCmd
    return;
  }

  // Update dot repeat register for buffer-modifying Normal mode commands.
  // Done pre-execution; pure motions and mode-entering commands are excluded.
  if (lastEditCmd && mode == Mode::Normal) {
    bool isMotion = false;
    if (e.size() == 1) {
      switch (e[0]) {
        case 'j': case 'k': case 'h': case 'l':
        case 'w': case 'W': case 'b': case 'B':
        case 'e': case 'E': case '0': case '^': case '$':
        case '}': case '{': case ')': case '(':
        case 'i': case 'I': case 'a': case 'A':
          isMotion = true; break;
        default: break;
      }
    } else if (e == "ge" || e == "gE") {
      isMotion = true;
    }
    if (!isMotion) {
      // Store full command including count prefix for correct dot repeat
      if (edit.hasCount()) {
        *lastEditCmd = to_string(count) + string(e);
      } else {
        *lastEditCmd = string(e);
      }
    }
  }

  int n = static_cast<int>(lines.size());

  // Past-end cursor: only valid after linewise deletion with hasLinesBelow.
  // The only valid command from here is 'k' to move back into the buffer.
  if (pos.line >= n) {
    assert(hasLinesBelow && "cursor past end only valid with hasLinesBelow");
    assert(e == "k" && "only 'k' is valid from past-end cursor");
    pos.line = max(0, pos.line - count);
    pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
    return;
  }

  string& line = lines[pos.line];
  int m = static_cast<int>(line.size());
  bool hasBoundaryContext =
      leftColOffset > 0 || rightColOffset > 0 || hasLinesAbove || hasLinesBelow;

  // Handle visual mode sequences first (before empty line check)
  // These are parsed as complete tokens like "vjd", "v}d", "vwhjd"
  if (mode == Mode::Normal && e.size() >= 2 && e[0] == 'v') {
    char op = e.back();  // 'd' or 'c'
    if (op != 'd' && op != 'c') {
      assert(false && "Visual mode sequence must end with 'd' or 'c'");
    }

    // Extract motion part (between 'v' and operator)
    string movementSeq(e.substr(1, e.size() - 2));

    // Record anchor position
    CursorPos anchor = pos;

    // Apply motions to get end position
    for (const auto& motion : parseEdits(movementSeq)) {
      applyEdit(lines, pos, mode, motion);
    }

    // Compute range (visual mode is inclusive of both endpoints in Vim).
    int posLineLen = static_cast<int>(lines[pos.line].size());
    CharRange r(anchor, CursorPos(pos.line, std::min(pos.col + 1, posLineLen)));
    r.normalize();

    // Apply operator
    if (op == 'd') {
      VimCore::deleteRangeAndUpdatePos(lines, r, pos);
    } else {  // op == 'c'
      VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
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
      case hash("dj"): case hash("dk"):
      case hash("cw"): case hash("cW"): case hash("cb"): case hash("cB"):
      case hash("ce"): case hash("cE"): case hash("cge"): case hash("cgE"):
      case hash("c}"): case hash("c{"): case hash("c)"): case hash("c("):
      case hash("0"): case hash("^"): case hash("$"):
      // Navigation (for TransformOptimizer line traversal)
      case hash("j"): case hash("k"): case hash("h"): case hash("l"):
      // Paragraph/sentence motions
      case hash("}"): case hash("{"): case hash(")"): case hash("("):
        break;  // Fall through to main switch
      default:
        std::string msg = "Edit " + std::string(e) + " invalid on empty line";
        const char* res = msg.c_str();
        assert(false && res);
    }
  }

  if (mode == Mode::Normal) {
    // Handle r{char} specially. Recall this fails if not enough characters.
    if (e.size() == 2 && e[0] == 'r') {
      if (pos.col + count > m) {
        assert(false && "r{char} requires more chars than available");
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
          assert(false && "x requires more chars");
        }
        line.erase(pos.col, count);
        pos.setCol(clampedToLastChar(line, pos.col));
        return;

      case hash("X"):
        if (count > pos.col) {
          assert(false && "X requires more chars before cursor");
        }
        line.erase(pos.col - count, count);
        pos.setCol(pos.col - count);
        return;

      case hash("~"):
        if (pos.col + count > m) {
          assert(false && "~ requires more chars");
        }
        for (int i = 0; i < count; i++) {
          char& c = line[pos.col + i];
          if (isupper(c)) c = tolower(c);
          else if (islower(c)) c = toupper(c);
        }
        pos.setCol(pos.col + count - 1);
        return;

      case hash("J"): {
        // {count}J joins max(count, 2) lines = max(count, 2) - 1 join operations
        int joinOps = max(count, 2) - 1;
        if (pos.line + joinOps >= n) {
          assert(false && "J requires more lines below");
        }
        for (int i = 0; i < joinOps; i++) VimCore::joinLines(lines, pos, true);
        return;
      }

      case hash("gJ"): {
        int joinOps = max(count, 2) - 1;
        if (pos.line + joinOps >= n) {
          assert(false && "gJ requires more lines below");
        }
        for (int i = 0; i < joinOps; i++) VimCore::joinLines(lines, pos, false);
        return;
      }

      case hash("dd"):
        if (pos.line + count > n) {
          assert(false && "dd requires more lines");
        }
        lines.erase(lines.begin() + pos.line, lines.begin() + pos.line + count);
        // Maintain invariant: buffer always has at least one line
        if (lines.empty()) {
          lines.push_back("");
        }
        if (hasLinesBelow && pos.line >= static_cast<int>(lines.size())) {
          // Cursor past end: real buffer has lines below. Leave pos.line
          // unclamped; caller will apply 'k' to move back in range.
          return;
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
          assert(false && "s requires more chars");
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
        if (hasBoundaryContext && count == 1) {
          bool big = (e == "dW");
          EdgeType endpointEdge = EdgeType::GapEdge;
          CursorPos endpoint = VimCore::motionWordEndpoint<true, EdgeType::GapEdge>(
              pos, lines, big, false,
              rightColOffset, hasLinesBelow, /*lineBounded=*/false);
          if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == pos) {
            assert(false && "dw/dW has no effect or crosses boundary");
          }
          // Match TransformExplorer semantics: dw/dW do not cross lines.
          if (endpoint.line > pos.line) {
            endpoint = VimCore::motionWordEndpoint<true, EdgeType::WordEdge>(
                pos, lines, big, false,
                rightColOffset, hasLinesBelow, /*lineBounded=*/false);
            if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == pos) {
              assert(false && "dw/dW has no effect or crosses boundary");
            }
            endpointEdge = EdgeType::WordEdge;
          }
          CursorPos end = VimCore::wordEndpointToRangeEnd(endpoint, lines, endpointEdge);
          VimCore::deleteRangeAndUpdatePos(lines, CharRange(pos, end),
                               pos, Mode::Normal);
          return;
        }
        deleteToNextWord(lines, pos, count, e == "dW", Mode::Normal);
        return;

      case hash("de"):
      case hash("dE"):
        if (hasBoundaryContext && count == 1) {
          bool big = (e == "dE");
          CursorPos endpoint = VimCore::motionWordEndpoint<true, EdgeType::WordEdge>(
              pos, lines, big, true,
              rightColOffset, hasLinesBelow, /*lineBounded=*/false);
          if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == pos) {
            assert(false && "de/dE has no effect or crosses boundary");
          }
          CursorPos end =
              VimCore::wordEndpointToRangeEnd(endpoint, lines, EdgeType::WordEdge);
          VimCore::deleteRangeAndUpdatePos(lines, CharRange(pos, end),
                               pos, Mode::Normal);
          return;
        }
        deleteToWordEnd(lines, pos, count, e == "dE", Mode::Normal);
        return;

      case hash("db"):
      case hash("dB"):
        if (hasBoundaryContext && count == 1) {
          bool big = (e == "dB");
          CursorPos endpoint = VimCore::motionWordEndpoint<false, EdgeType::WordEdge>(
              pos, lines, big, true,
              leftColOffset, hasLinesAbove, /*lineBounded=*/false);
          if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == pos) {
            assert(false && "db/dB has no effect or crosses boundary");
          }

          CursorPos end = POSITION_OUTSIDE_BOUNDARY;
          int cursorContentCol = pos.col - (pos.line == 0 ? leftColOffset : 0);
          if (cursorContentCol > 0) {
            end = CursorPos(pos.line, pos.col);
          } else if (endpoint.line < pos.line) {
            int prevLine = pos.line - 1;
            end = CursorPos(prevLine, static_cast<int>(lines[prevLine].size()));
          }

          if (end == POSITION_OUTSIDE_BOUNDARY) {
            assert(false && "db/dB has no effect at boundary");
          }
          CharRange range(endpoint, end);
          int oldLineCount = static_cast<int>(lines.size());
          CursorPos originalPos = pos;
          VimCore::deleteRangeAndUpdatePos(lines, range, pos, Mode::Normal);
          adjustCursorAfterBackwardWordDelete(
              range, oldLineCount, originalPos, lines, pos, leftColOffset);
          return;
        }
        if (pos.line == 0 && pos.col == 0) {
          assert(false && "db/dB at start of buffer has no effect");
        }
        deleteBackToWordStart(lines, pos, count, e == "dB", Mode::Normal);
        return;

      case hash("dge"):
      case hash("dgE"):
        if (hasBoundaryContext && count == 1) {
          bool big = (e == "dgE");
          if (inBoundaryRegion(pos, lines, leftColOffset, rightColOffset)) {
            assert(false && "dge/dgE has no effect in boundary region");
          }

          CursorPos endpoint = VimCore::motionWordEndpoint<false, EdgeType::NextEdge>(
              pos, lines, big, true,
              leftColOffset, hasLinesAbove, /*lineBounded=*/false);
          if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == pos) {
            assert(false && "dge/dgE has no effect or crosses boundary");
          }
          int lineLen = static_cast<int>(lines[pos.line].size());
          VimCore::deleteRangeAndUpdatePos(lines, CharRange(endpoint, CursorPos(pos.line, std::min(pos.col + 1, lineLen))),
                               pos, Mode::Normal);
          return;
        }
        if (pos.line == 0 && pos.col == 0) {
          assert(false && "dge/dgE at start of buffer has no effect");
        }
        deleteBackToWordEnd(lines, pos, count, e == "dgE", Mode::Normal);
        return;

      // dj: linewise delete current line + count lines below
      case hash("dj"):
        {
          int endLine = pos.line + count + 1;
          if (endLine > n) {
            assert(false && "dj requires more lines below");
          }
          VimCore::deleteLineRangeAndUpdatePos(lines, LineRange(pos.line, endLine), pos, hasLinesBelow);
        }
        return;

      // dk: linewise delete current line + count lines above
      case hash("dk"):
        {
          int beginLine = pos.line - count;
          if (beginLine < 0) {
            assert(false && "dk requires more lines above");
          }
          VimCore::deleteLineRangeAndUpdatePos(lines, LineRange(beginLine, pos.line + 1), pos, hasLinesBelow);
        }
        return;

      // cj: change current line + count lines below (linewise)
      case hash("cj"):
        {
          int lastLine = pos.line + count;
          if (lastLine >= n) {
            assert(false && "cj requires more lines below");
          }
          // cj deletes lines and replaces with empty insert line
          lines.erase(lines.begin() + pos.line, lines.begin() + lastLine + 1);
          lines.insert(lines.begin() + pos.line, "");
          pos.setCol(0);
          mode = Mode::Insert;
        }
        return;

      // ck: change current line + count lines above (linewise)
      case hash("ck"):
        {
          int beginLine = pos.line - count;
          if (beginLine < 0) {
            assert(false && "ck requires more lines above");
          }
          lines.erase(lines.begin() + beginLine, lines.begin() + pos.line + 1);
          lines.insert(lines.begin() + beginLine, "");
          pos.line = beginLine;
          pos.setCol(0);
          mode = Mode::Insert;
        }
        return;

      case hash("d0"):
        if (count > 1) {
          debug("d0: count", count, "ignored (0 motion doesn't use count)");
        }
        if (pos.col == 0) {
          assert(false && "d0 at column 0 has no effect");
        }
        {
          CharRange r(CursorPos(pos.line, 0), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
        }
        return;

      case hash("d^"):
        if (count > 1) {
          debug("d^: count", count, "ignored (^ motion doesn't use count)");
        }
        {
          int firstNonBlank = VimCore::firstNonBlankColInLineStr(lines[pos.line]);
          if (firstNonBlank >= pos.col) {
            assert(false && "d^ at or before first non-blank has no effect");
          }
          CharRange r(CursorPos(pos.line, firstNonBlank), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
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
            CharRange r(pos, CursorPos(pos.line, endCol + 1));
            VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
          } else {
            // On whitespace: use w/W motion (change to start of next word)
            CursorPos goalPos = pos;
            for (int i = 0; i < count; i++) VimCore::motionW(goalPos, lines, big);
            if (shouldStopAtEndOfLine(count, pos, goalPos, lines)) {
              deleteToEndOfLine(lines, pos);
            } else if (isPastEndPosition(lines, goalPos)) {
              // w motion wanted to go past EOF - delete to last char inclusive
              goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
              if (goalPos.line > pos.line || goalPos.col >= pos.col) {
                CharRange r(pos, CursorPos(goalPos.line, goalPos.col + 1));
                VimCore::deleteRangeAndUpdatePos(lines, r, pos);
              }
            } else if (goalPos.line > pos.line || goalPos.col > pos.col) {
              // Normal case: goalPos is already exclusive, except col 0 on next line.
              CursorPos end;
              if (goalPos.col > 0) {
                end = goalPos;
              } else {
                // goalPos at col 0 of new line - delete to end of previous line.
                end = CursorPos(goalPos.line - 1, static_cast<int>(lines[goalPos.line - 1].size()));
              }
              CharRange r(pos, end);
              VimCore::deleteRangeAndUpdatePos(lines, r, pos);
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
          assert(false && "cb/cB at start of buffer has no effect");
        }
        {
          bool big = (e == "cB");
          CursorPos initialPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionB(initialPos, lines, big);
          if (initialPos < pos) {
            // For cb/cB, don't delete across newline boundaries (same as cw/cW)
            // If motion crossed to previous line and we're at col 0, only delete
            // to end of the line where b landed
            CursorPos goalPos;
            if (initialPos.line < pos.line && pos.col == 0) {
              // Delete to end of the line where b landed
              int lastCol = static_cast<int>(lines[initialPos.line].size()) - 1;
              goalPos = CursorPos(initialPos.line, lastCol >= 0 ? lastCol : 0);
            } else {
              goalPos = CursorPos(pos.line, pos.col > 0 ? pos.col - 1 : 0);
            }
            if (initialPos <= goalPos) {
              CursorPos end =
                  (initialPos.line < pos.line && pos.col == 0)
                      ? CursorPos(initialPos.line, static_cast<int>(lines[initialPos.line].size()))
                      : CursorPos(pos.line, pos.col);
              CharRange r(initialPos, end);
              VimCore::deleteRangeAndUpdatePos(lines, r, pos);
            }
          }
          mode = Mode::Insert;
        }
        return;

      case hash("cge"):
      case hash("cgE"):
        if (pos.line == 0 && pos.col == 0) {
          assert(false && "cge/cgE at start of buffer has no effect");
        }
        deleteBackToWordEnd(lines, pos, count, e == "cgE", Mode::Insert);
        mode = Mode::Insert;
        return;

      case hash("c0"):
        if (count > 1) {
          debug("c0: count", count, "ignored (0 motion doesn't use count)");
        }
        if (pos.col == 0) {
          assert(false && "c0 at column 0 has no effect");
        }
        {
          CharRange r(CursorPos(pos.line, 0), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
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
            assert(false && "c^ at or before first non-blank has no effect");
          }
          CharRange r(CursorPos(pos.line, firstNonBlank), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
        }
        mode = Mode::Insert;
        return;

      case hash("C"):
      case hash("c$"):
        if (pos.line + count > n) {
          assert(false && "c$ requires more lines than available");
        }
        {
          int endLine = pos.line + count - 1;
          int endColExclusive = static_cast<int>(lines[endLine].size());
          CharRange r(pos, CursorPos(endLine, endColExclusive));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
          mode = Mode::Insert;
        }
        return;

      case hash("D"):
      case hash("d$"):
        if (pos.line + count > n) {
          assert(false && "d$ requires more lines than available");
        }
        {
          int endLine = pos.line + count - 1;
          int endColExclusive = static_cast<int>(lines[endLine].size());
          CharRange r(pos, CursorPos(endLine, endColExclusive));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
        }
        return;

      // --- Paragraph motions ---
      // Both } and { are exclusive motions resolved to characterwise or linewise.
      // See VimEditUtils.h for the full rule description and examples.
      case hash("d}"):
      case hash("c}"):
        {
          CursorPos goalPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionParagraphNext(goalPos, lines);
          // Paragraph-specific: } at EOF (last non-blank line) is inclusive.
          bool atEof = goalPos.line == lines.lastLine()
                    && !VimCore::isBlankLineStr(lines[goalPos.line]);
          if (goalPos > pos || (atEof && goalPos >= pos)) {
            CharRange range(pos, goalPos);
            if (atEof) {
              // Inclusive at EOF: extend end by one char to make half-open.
              int eofLineLen = static_cast<int>(lines[goalPos.line].size());
              range.end = CursorPos(goalPos.line, std::min(goalPos.col + 1, eofLineLen));
            }
            // Forward: exclusive end = goalPos (motion destination).
            auto resolved = VimCore::resolveExclusiveDeleteRange(range, lines, e[0] == 'd');
            applyResolvedDeleteRange(
                lines, pos, resolved, e[0] == 'c' ? Mode::Insert : Mode::Normal);
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      case hash("d{"):
      case hash("c{"):
        {
          CursorPos initialPos = pos;
          for (int i = 0; i < count; i++) VimCore::motionParagraphPrev(initialPos, lines);
          // Backward: exclusive end = cursor pos (the higher end of the range).
          if (initialPos < pos) {
            CharRange range(initialPos, pos);
            auto resolved = VimCore::resolveExclusiveDeleteRange(range, lines, e[0] == 'd');
            applyResolvedDeleteRange(
                lines, pos, resolved, e[0] == 'c' ? Mode::Insert : Mode::Normal);
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      // --- Sentence motions ---
      // Both ) and ( are exclusive motions resolved to characterwise or linewise.
      // See VimEditUtils.h for the full rule description and examples.
      case hash("d)"):
      case hash("c)"):
        {
          CursorPos goalPos = pos;
          for (int i = 0; i < count; i++) {
            if (hasBoundaryContext) {
              CursorPos endpoint =
                  VimCore::motionSentenceEndpoint<true, SentenceEdgeType::NextEdge>(
                      goalPos, lines, rightColOffset, hasLinesBelow);
              if (endpoint == POSITION_OUTSIDE_BOUNDARY) {
                goalPos = POSITION_OUTSIDE_BOUNDARY;
                break;
              }
              goalPos = endpoint;
            } else {
              VimCore::motionSentenceNext(goalPos, lines);
            }
          }
          // Forward: exclusive end = goalPos (motion destination).
          if (goalPos != POSITION_OUTSIDE_BOUNDARY && goalPos > pos) {
            auto resolved = VimCore::resolveExclusiveDeleteRange(
                CharRange(pos, goalPos), lines, e[0] == 'd');
            applyResolvedDeleteRange(
                lines, pos, resolved, e[0] == 'c' ? Mode::Insert : Mode::Normal);
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      case hash("d("):
      case hash("c("):
        {
          CursorPos initialPos = pos;
          for (int i = 0; i < count; i++) {
            if (hasBoundaryContext) {
              CursorPos endpoint =
                  VimCore::motionSentenceEndpoint<false, SentenceEdgeType::NextEdge>(
                      initialPos, lines, leftColOffset, hasLinesAbove);
              if (endpoint == POSITION_OUTSIDE_BOUNDARY) {
                initialPos = POSITION_OUTSIDE_BOUNDARY;
                break;
              }
              initialPos = endpoint;
            } else {
              VimCore::motionSentencePrev(initialPos, lines);
            }
          }
          // Backward: exclusive end = cursor pos (the higher end of the range).
          if (initialPos != POSITION_OUTSIDE_BOUNDARY && initialPos < pos) {
            auto resolved = VimCore::resolveExclusiveDeleteRange(
                CharRange(initialPos, pos), lines, e[0] == 'd');
            applyResolvedDeleteRange(
                lines, pos, resolved, e[0] == 'c' ? Mode::Insert : Mode::Normal);
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      // --- Navigation motions (for TransformOptimizer) ---
      case hash("j"):
        if (pos.line + count >= n) {
          assert(false && "j requires more lines below");
        }
        pos.line += count;
        pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
        return;

      case hash("k"):
        pos.line = max(0, pos.line - count);
        pos.clampColPreservingTarget(VimCore::clampCol(lines, pos.targetCol, pos.line));
        return;

      case hash("h"):
        if (pos.col < count) {
          assert(false && "h requires more chars left");
        }
        pos.setCol(pos.col - count);
        return;

      case hash("l"):
        if (pos.col + count >= m) {
          assert(false && "l requires more chars right");
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

      CharRange r(pos, pos);  // Default: empty range

      // Word objects - use VimCore
      if (obj == 'w' || obj == 'W') {
        Lines linesWrapper(lines.begin(), lines.end());
        bool bigWord = (obj == 'W');
        bool hasBoundaryContext = leftColOffset > 0 || rightColOffset > 0 ||
                                  hasLinesAbove || hasLinesBelow;
        if (hasBoundaryContext) {
          r = VimCore::textObjectRange(
              pos, linesWrapper, inner, bigWord,
              leftColOffset, rightColOffset, hasLinesAbove, hasLinesBelow);
        } else {
          r = VimCore::textObject(pos, linesWrapper, inner, bigWord);
        }
      }
      // Quote objects
      else if (obj == '"' || obj == '\'' || obj == '`') {
        r = inner ? VimTextObjectsLegacy::innerQuote(lines, pos, obj)
                  : VimTextObjectsLegacy::aroundQuote(lines, pos, obj);
      }
      // Bracket objects - handle both opening and closing chars
      else if (obj == '(' || obj == ')' || obj == 'b') {
        r = inner ? VimTextObjectsLegacy::innerBracket(lines, pos, '(', ')')
                  : VimTextObjectsLegacy::aroundBracket(lines, pos, '(', ')');
      } else if (obj == '{' || obj == '}' || obj == 'B') {
        r = inner ? VimTextObjectsLegacy::innerBracket(lines, pos, '{', '}')
                  : VimTextObjectsLegacy::aroundBracket(lines, pos, '{', '}');
      } else if (obj == '[' || obj == ']') {
        r = inner ? VimTextObjectsLegacy::innerBracket(lines, pos, '[', ']')
                  : VimTextObjectsLegacy::aroundBracket(lines, pos, '[', ']');
      } else if (obj == '<' || obj == '>') {
        r = inner ? VimTextObjectsLegacy::innerBracket(lines, pos, '<', '>')
                  : VimTextObjectsLegacy::aroundBracket(lines, pos, '<', '>');
      } else {
        assert(false && "Unknown text object");
      }

      // Apply operator to range (all ranges are half-open [begin, end)).
      if (r.isValid()) {
        if (op == 'd') {
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
        } else {  // op == 'c'
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
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
          assert(false && "<BS> at start of buffer has no effect");
        }
        if (pos.col == 0) {
          // Join with previous line
          int prevLen = static_cast<int>(lines[pos.line - 1].size());
          CursorPos joinPos(pos.line - 1, 0);
          VimCore::joinLines(lines, joinPos, false);
          pos = CursorPos(pos.line - 1, prevLen);
        } else {
          // Delete char before cursor
          CursorPos beforePos(pos.line, pos.col - 1);
          CharRange r(beforePos, CursorPos(beforePos.line, beforePos.col + 1));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
        }
        return;

      case hash("<Del>"):
        {
          int len = static_cast<int>(lines[pos.line].size());
          if (pos.col >= len && pos.line + 1 >= static_cast<int>(lines.size())) {
            assert(false && "<Del> at end of buffer has no effect");
          }
          if (pos.col >= len) {
            // At end of line - join with next line
            VimCore::joinLines(lines, pos, false);
          } else {
            // Delete char at cursor
            CharRange r(pos, CursorPos(pos.line, pos.col + 1));
            VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
          }
        }
        return;

      case hash("<CR>"):
        VimCore::insertText(lines, pos, "\n");
        return;

      case hash("<C-u>"):
        if (pos.col == 0) {
          assert(false && "<C-u> at start of line has no effect");
        }
        {
          CharRange r(CursorPos(pos.line, 0), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
        }
        return;

      case hash("<C-w>"):
        if (pos.col == 0) {
          assert(false && "<C-w> at start of line has no effect");
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
          CharRange r(CursorPos(pos.line, col), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
        }
        return;

      case hash("<Left>"):
        if (pos.col == 0) {
          assert(false && "<Left> at start of line has no effect");
        }
        pos.setCol(pos.col - 1);
        return;

      case hash("<Right>"):
        if (pos.col >= static_cast<int>(lines[pos.line].size())) {
          assert(false && "<Right> at end of line has no effect");
        }
        pos.setCol(pos.col + 1);
        return;

      case hash("<Up>"):
        if (pos.line == 0) {
          assert(false && "<Up> at first line has no effect");
        }
        pos.line--;
        // Insert mode <Up>/<Down> use sticky column behavior like j/k
        pos.clampColPreservingTarget(min(pos.targetCol, static_cast<int>(lines[pos.line].size())));
        return;

      case hash("<Down>"):
        if (pos.line + 1 >= static_cast<int>(lines.size())) {
          assert(false && "<Down> at last line has no effect");
        }
        pos.line++;
        // Insert mode <Up>/<Down> use sticky column behavior like j/k
        pos.clampColPreservingTarget(min(pos.targetCol, static_cast<int>(lines[pos.line].size())));
        return;
    }
  }

  assert(false && "Unknown edit");
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
      constexpr int INT_MAX_VALUE = std::numeric_limits<int>::max();
      while (i < sv.size() && isdigit(sv[i])) {
        int digit = sv[i] - '0';
        if (cnt > (INT_MAX_VALUE - digit) / 10) {
          cnt = INT_MAX_VALUE;
        } else {
          cnt = cnt * 10 + digit;
        }
        i++;
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
      assert(false && "Malformed special key");
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
