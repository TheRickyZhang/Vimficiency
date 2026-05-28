#include "EditInterpreter.h"
#include "Interpreter/EditHash.h"
#include "VimCore/CharMask.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimOptions.h"
#include "VimCore/VimCore.h"
#include "Keyboard/KeyNotation.h"
#include "Utils/Debug.h"
#include "Types/Lines.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <optional>

using namespace std;

namespace Edit {

string formatEditParseError(const EditParseError& error) {
  switch (error.kind) {
    case EditParseErrorKind::MalformedSpecialKey:
      return "Malformed special key at byte offset " + to_string(error.offset);
  }
  assert(false && "Unhandled EditParseErrorKind");
  return "unknown parse error";
}

inline int clampedToLastChar(const string& line, int col) {
  return line.empty() ? 0 : min(col, (int)line.size() - 1);
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

static void applyExclusiveMotionEdit(Lines& lines,
                                     CursorPos& pos,
                                     Mode& mode,
                                     CharRange range,
                                     VimCore::LineDeleteContext lineDeleteContext,
                                     bool change) {
  auto resolved = change
      ? VimCore::resolveExclusiveChangeRange(range, lines)
      : VimCore::resolveExclusiveDeleteRange(range, lines, true);
  if (change && resolved.kind == VimCore::ResolvedDeleteRangeKind::Linewise) {
    LineRange lineRange = resolved.lineRange;
    lineRange.normalize();
    VimCore::linewiseChangeWithAutoindent(
        lines, pos, mode,
        lineRange.beginLine, lineRange.endLine - 1, lines[lineRange.beginLine]);
    return;
  }
  VimCore::deleteResolvedRangeAndUpdatePos(
      lines, resolved, pos, change ? Mode::Insert : Mode::Normal,
      lineDeleteContext);
  if (change) mode = Mode::Insert;
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
  // For empty lines, col 0 IS the cursor position (not past end). Only treat
  // col == size as past-end when size > 0.
  int size = static_cast<int>(lines[pos.line].size());
  return size > 0 && pos.col == size;
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

static VimCore::WordBoundaryContext makeWordBoundaryContext(
    int leftColOffset, int rightColOffset,
    bool hasLinesAbove, bool hasLinesBelow) {
  VimCore::WordBoundaryContext boundary;
  boundary.leftColOffset = leftColOffset;
  boundary.rightColOffset = rightColOffset;
  boundary.hasLinesAbove = hasLinesAbove;
  boundary.hasLinesBelow = hasLinesBelow;
  boundary.clampOutside = false;
  return boundary;
}

bool updatesDotRepeat(Mode mode, string_view e) {
  if (mode != Mode::Normal || e == ".") return false;

  if (e.size() == 1) {
    switch (e[0]) {
      case 'j': case 'k': case 'h': case 'l':
      case 'w': case 'W': case 'b': case 'B':
      case 'e': case 'E': case '0': case '^': case '$':
      case '}': case '{': case ')': case '(':
      case 'i': case 'I': case 'a': case 'A':
        return false;
      default:
        return true;
    }
  }

  return e != "ge" && e != "gE";
}

bool isValidNormalEditOnEmptyLine(string_view e) {
  switch (hash(e)) {
    case hash("i"): case hash("a"): case hash("I"): case hash("A"):
    case hash("o"): case hash("O"): case hash("dd"): case hash("cc"): case hash("S"):
    case hash("J"): case hash("gJ"):
    // Char-delete operators on empty line: Vim no-ops, doesn't beep.
    case hash("x"): case hash("X"):
    // Dot-repeat: only valid if there's something to repeat. We assert at
    // dot-handling time; on empty line `.` itself is fine to dispatch.
    case hash("."):
    case hash("w"): case hash("W"): case hash("b"): case hash("B"):
    case hash("e"): case hash("E"): case hash("ge"): case hash("gE"):
    case hash("dw"): case hash("dW"): case hash("db"): case hash("dB"):
    case hash("de"): case hash("dE"): case hash("dge"): case hash("dgE"):
    case hash("d}"): case hash("d{"): case hash("d)"): case hash("d("):
    case hash("dj"): case hash("dk"):
    case hash("cw"): case hash("cW"): case hash("cb"): case hash("cB"):
    case hash("ce"): case hash("cE"): case hash("cge"): case hash("cgE"):
    case hash("c}"): case hash("c{"): case hash("c)"): case hash("c("):
    case hash("diw"): case hash("daw"): case hash("diW"): case hash("daW"):
    case hash("ciw"): case hash("caw"): case hash("ciW"): case hash("caW"):
    // Quote text objects: no-op on empty line (no quote to find).
    case hash("da\""): case hash("di\""): case hash("da'"): case hash("di'"):
    case hash("da`"): case hash("di`"):
    case hash("ca\""): case hash("ci\""): case hash("ca'"): case hash("ci'"):
    case hash("ca`"): case hash("ci`"):
    // Bracket text objects: no-op on empty line (no bracket to find).
    case hash("da("): case hash("di("): case hash("da)"): case hash("di)"):
    case hash("da{"): case hash("di{"): case hash("da}"): case hash("di}"):
    case hash("da["): case hash("di["): case hash("da]"): case hash("di]"):
    case hash("da<"): case hash("di<"): case hash("da>"): case hash("di>"):
    case hash("daB"): case hash("diB"): case hash("dab"): case hash("dib"):
    case hash("ca("): case hash("ci("): case hash("ca)"): case hash("ci)"):
    case hash("ca{"): case hash("ci{"): case hash("ca}"): case hash("ci}"):
    case hash("ca["): case hash("ci["): case hash("ca]"): case hash("ci]"):
    case hash("ca<"): case hash("ci<"): case hash("ca>"): case hash("ci>"):
    case hash("caB"): case hash("ciB"): case hash("cab"): case hash("cib"):
    // Sentence text objects: no-op on empty line (no sentence content).
    case hash("das"): case hash("dis"): case hash("cas"): case hash("cis"):
    // Paragraph text objects: handled linewise; safe to invoke on empty line.
    case hash("dap"): case hash("dip"): case hash("cap"): case hash("cip"):
    case hash("0"): case hash("^"): case hash("$"):
    case hash("j"): case hash("k"): case hash("h"): case hash("l"):
    case hash("}"): case hash("{"): case hash(")"): case hash("("):
    case hash("<Esc>"):
      return true;
    default:
      return false;
  }
}

optional<DotRepeat> dotRepeatFor(Mode mode, const ParsedEdit& edit) {
  if (!updatesDotRepeat(mode, edit.edit)) return nullopt;
  return DotRepeat{string(edit.edit), static_cast<int>(edit.rawCount())};
}

// -----------------------------------------------------------------------------
// Shared d/c operator helpers - compute range and delete with given mode
// These ensure d and c operators have identical range computation.
// -----------------------------------------------------------------------------

// de/dE/ce/cE: delete to end of word (inclusive motion)
static void deleteToWordEnd(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos goalPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionEForOperator(goalPos, lines, big);

  if (isPastEndPosition(lines, goalPos)) {
    // e motion wanted to go past EOF - delete to last char inclusive
    goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
  }

  // Inclusive delete: goalPos >= pos means we have something to delete
  // This handles the case where motion didn't move (single char at EOF)
  if (goalPos.line > pos.line || goalPos.col >= pos.col) {
    CharRange r(pos, VimCore::onePastOnSameLine(lines, goalPos));
    auto resolved = mode == Mode::Insert
        ? VimCore::resolveWordOperatorChangeMotion(
              VimCore::WordOperatorTarget::DeleteToWordEnd, r, pos, lines)
        : VimCore::resolveWordOperatorMotion(
              VimCore::WordOperatorTarget::DeleteToWordEnd, r, pos, lines);
    VimCore::deleteResolvedRangeAndUpdatePos(lines, resolved, pos, mode);
  }
}

// dw/dW: delete to next word start (exclusive motion, special line-crossing rule)
static void deleteToNextWord(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos goalPos = pos;
  for (int i = 0; i < count; i++) VimCore::motionWForOperator(goalPos, lines, big);

  if (shouldStopAtEndOfLine(count, pos, goalPos, lines)) {
    // Special case: single dw on non-empty line crossing to next line —
    // Vim's "for dw on last word of line, the newline is not included".
    deleteToEndOfLine(lines, pos);
  } else if (isPastEndPosition(lines, goalPos)) {
    // Motion wanted to go past EOF - delete to last char inclusive
    goalPos.setCol(static_cast<int>(lines[goalPos.line].size()) - 1);
    if (goalPos.line > pos.line || goalPos.col >= pos.col) {
      CharRange r(pos, VimCore::onePastOnSameLine(lines, goalPos));
      VimCore::deleteRangeAndUpdatePos(lines, r, pos, mode);
    }
  } else if (goalPos.line > pos.line || goalPos.col > pos.col) {
    // Standard exclusive motion. Let resolveExclusiveDeleteRange handle the
    // shape (characterwise / line-char / linewise per Vim's exclusive rules).
    // The "last word of line, don't include newline" rule above already
    // peeled off the only case where Vim deviates from standard exclusive.
    CharRange r(pos, goalPos);
    auto resolved = VimCore::resolveExclusiveDeleteRange(r, lines, true);
    VimCore::deleteResolvedRangeAndUpdatePos(lines, resolved, pos, mode);
  }
}

// db/dB: delete backward to word start (exclusive motion)
static void deleteBackToWordStart(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos initialPos = pos;
  // `bckWordOperator` returns false only when Vim's bck_word would FAIL — i.e.,
  // the FIRST dec_cursor at the start of any iteration hits the start of the
  // buffer. Mid-iteration `dec_cursor()==-1` returns OK, so partial counts
  // succeed gracefully (matching Vim's observable behavior).
  if (!VimCore::bckWordOperator(initialPos, lines, big, count)) {
    pos = initialPos;
    return;
  }

  if (initialPos < pos) {
    auto resolved = VimCore::resolveWordOperatorMotion(
        VimCore::WordOperatorTarget::DeleteBackToWordBegin,
        CharRange(initialPos, pos), pos, lines);
    VimCore::deleteResolvedRangeAndUpdatePos(lines, resolved, pos, mode);
  }
}

// dge/dgE: delete backward to previous word end (inclusive motion)
static void deleteBackToWordEnd(Lines& lines, CursorPos& pos, int count, bool big, Mode mode) {
  CursorPos initialPos = pos;
  // `bckEndWordOperator` returns false only on Vim's bckend_word FAIL — i.e.,
  // the initial dec_cursor at the start of an iteration hits start of buffer.
  // Mid-iteration `dec_cursor()==-1` returns OK, so partial counts succeed.
  if (!VimCore::bckEndWordOperator(initialPos, lines, big, count)) {
    pos = initialPos;
    return;
  }

  if (initialPos < pos) {
    auto resolved = mode == Mode::Insert
        ? VimCore::resolveWordOperatorChangeMotion(
              VimCore::WordOperatorTarget::DeleteBackToWordEnd,
              CharRange(initialPos, pos), pos, lines)
        : VimCore::resolveWordOperatorMotion(
              VimCore::WordOperatorTarget::DeleteBackToWordEnd,
              CharRange(initialPos, pos), pos, lines);
    VimCore::deleteResolvedRangeAndUpdatePos(lines, resolved, pos, mode);
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
               DotRepeat* lastEdit, bool hasLinesBelow,
               int leftColOffset, int rightColOffset,
               bool hasLinesAbove, int* pendingAutoindentLen) {
  // Lines invariant: buffer always has at least one line (minimum: {""})
  assert(!lines.empty());

  string_view e = edit.edit;
  int count = edit.effectiveCount();
  size_t h = hash(e);

  // Dot repeat: replay the last buffer-modifying command. With nothing to
  // repeat (`.` after no prior edit, or after edits that all no-oped), Vim
  // beeps and does nothing — match that by returning.
  if (mode == Mode::Normal && e == ".") {
    if (!lastEdit || lastEdit->empty()) return;
    ParsedEdit repeat = lastEdit->asEdit();
    // Explicit count on '.' overrides the stored count
    if (edit.hasCount()) {
      repeat = ParsedEdit{repeat.edit, count};
    }
    applyEdit(lines, pos, mode, repeat, nullptr, hasLinesBelow,
              leftColOffset, rightColOffset, hasLinesAbove);  // don't update lastEdit
    return;
  }

  // Update dot repeat register for buffer-modifying Normal mode commands.
  // Vim doesn't update the redo register for no-op edits (`x` on an empty
  // line, `Ndd` when fewer than N lines remain, etc.). We approximate that
  // by snapshotting the buffer/cursor here and only committing the new
  // dot-cmd if the execution actually changed something. The proposed update
  // is staged here; commit happens at end of applyEdit.
  optional<DotRepeat> stagedDot;
  Lines preLines;
  CursorPos prePos = pos;
  if (lastEdit) {
    stagedDot = dotRepeatFor(mode, edit);
    if (stagedDot) {
      preLines = lines;
    }
  }
  // Mode is intentionally ignored: `cc`/`S` on an empty line legitimately
  // transitions Normal→Insert without modifying the buffer, and Vim doesn't
  // update the redo register for that case (the dot-register update happens
  // at `<Esc>` time, gated on whether anything was typed). We approximate by
  // treating "buffer+cursor unchanged" as a no-op.
  struct DotCommitGuard {
    DotRepeat* slot;
    optional<DotRepeat>& staged;
    const Lines& pre;
    const Lines& post;
    const CursorPos& prePos;
    const CursorPos& postPos;
    ~DotCommitGuard() {
      if (!slot || !staged) return;
      if (pre == post && prePos.line == postPos.line &&
          prePos.col == postPos.col) {
        return;
      }
      *slot = std::move(*staged);
    }
  } dotGuard{lastEdit, stagedDot, preLines, lines, prePos, pos};

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
  VimCore::LineDeleteContext lineDeleteContext{
      .hasLinesAbove = hasLinesAbove,
      .hasLinesBelow = hasLinesBelow,
  };

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
    auto motions = parseEdits(movementSeq);
    assert(motions);
    for (const auto& motion : *motions) {
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
    if (!isValidNormalEditOnEmptyLine(e)) {
      std::string msg = "Edit " + std::string(e) + " invalid on empty line";
      const char* res = msg.c_str();
      assert(false && res);
    }
  }

  if (mode == Mode::Normal) {
    // <Esc> in Normal mode is a no-op (matches Vim: clears any pending
    // operator but otherwise does nothing). Reached when a c-operator
    // text-object motion failed and didn't transition to Insert mode.
    if (e == "<Esc>") {
      return;
    }
    // Handle r{char} specially. Recall this fails if not enough characters.
    if (e.size() >= 2 && e[0] == 'r') {
      optional<char> target;
      if (e.size() == 2) {
        target = e[1];
      } else if (e[1] == '<') {
        target = parseDisplayChar(e.substr(1));
        if (!target.has_value() && (e == "r<Esc>" || e == "r<Escape>")) {
          return;
        }
      }
      if (!target.has_value()) {
        assert(false && "unsupported r{char} target");
      }
      if (pos.col + count > m) {
        // Not enough chars to replace — Vim no-ops with a beep.
        return;
      }
      for (int i = 0; i < count; i++) {
        line[pos.col + i] = *target;
      }
      pos.setCol(pos.col + count - 1);
      return;
    }

    switch (hash(e)) {
      case hash("x"):
        // Vim clamps count to chars remaining at or after cursor.
        count = std::min(count, m - pos.col);
        if (count > 0) line.erase(pos.col, count);
        pos.setCol(clampedToLastChar(line, pos.col));
        return;

      case hash("X"):
        // Vim limits count to chars available before cursor (silent clamp).
        count = std::min(count, pos.col);
        if (count > 0) {
          line.erase(pos.col - count, count);
          pos.setCol(pos.col - count);
        }
        return;

      case hash("~"):
        // Vim clamps count to chars remaining at or after cursor.
        count = std::min(count, m - pos.col);
        for (int i = 0; i < count; i++) {
          char& c = line[pos.col + i];
          if (isupper(c)) c = tolower(c);
          else if (islower(c)) c = toupper(c);
        }
        pos.setCol(clampedToLastChar(line, pos.col + count));
        return;

      case hash("J"): {
        int lineCount = max(count, 2);
        if (pos.line + lineCount > n) {
          // Not enough lines below — Vim no-ops with a beep.
          return;
        }
        if (lineCount == 2) {
          VimCore::joinLines(lines, pos, true);
        } else {
          VimCore::joinLineRange(lines, pos, lineCount, true);
        }
        return;
      }

      case hash("gJ"): {
        int lineCount = max(count, 2);
        if (pos.line + lineCount > n) {
          return;  // no-op, matches Vim
        }
        if (lineCount == 2) {
          VimCore::joinLines(lines, pos, false);
        } else {
          VimCore::joinLineRange(lines, pos, lineCount, false);
        }
        return;
      }

      case hash("dd"):
        // Vim's rule: `Ndd` with N >= 2 requires the cursor to NOT be at the
        // last line (needs N-1 lines below). If insufficient, no-op (beep).
        // For N == 1 (plain `dd`), always delete the cursor line.
        if (count >= 2 && pos.line >= n - 1) return;
        count = std::min(count, n - pos.line);
        if (count <= 0) return;
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
          pos.setCol(VimCore::firstNonBlankColInLine(lines[pos.line]));
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
        VimCore::linewiseChangeWithAutoindent(lines, pos, mode, pos.line, pos.line, lines[pos.line]);
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
        // Vim's `I` lands at first non-blank; on an all-whitespace line it
        // instead moves past the indent (col == line size).
        pos.setCol(VimCore::isBlankLine(lines[pos.line])
                       ? static_cast<int>(lines[pos.line].size())
                       : VimCore::firstNonBlankColInLine(lines[pos.line]));
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
          CharRange range = VimCore::wordOperatorRange(
              pos, lines, VimCore::WordOperatorTarget::DeleteToNextWord,
              e == "dW",
              makeWordBoundaryContext(leftColOffset, rightColOffset,
                                      hasLinesAbove, hasLinesBelow));
          if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
              range.end == POSITION_OUTSIDE_BOUNDARY) {
            assert(false && "dw/dW has no effect or crosses boundary");
          }
          auto resolved = VimCore::resolveWordOperatorMotion(
              VimCore::WordOperatorTarget::DeleteToNextWord,
              range, pos, lines, leftColOffset);
          VimCore::deleteResolvedRangeAndUpdatePos(
              lines, resolved, pos, Mode::Normal, lineDeleteContext);
          return;
        }
        deleteToNextWord(lines, pos, count, e == "dW", Mode::Normal);
        return;

      case hash("de"):
      case hash("dE"):
        if (hasBoundaryContext && count == 1) {
          CharRange range = VimCore::wordOperatorRange(
              pos, lines, VimCore::WordOperatorTarget::DeleteToWordEnd,
              e == "dE",
              makeWordBoundaryContext(leftColOffset, rightColOffset,
                                      hasLinesAbove, hasLinesBelow));
          if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
              range.end == POSITION_OUTSIDE_BOUNDARY) {
            assert(false && "de/dE has no effect or crosses boundary");
          }
          auto resolved = VimCore::resolveWordOperatorMotion(
              VimCore::WordOperatorTarget::DeleteToWordEnd,
              range, pos, lines, leftColOffset);
          VimCore::deleteResolvedRangeAndUpdatePos(
              lines, resolved, pos, Mode::Normal, lineDeleteContext);
          return;
        }
        deleteToWordEnd(lines, pos, count, e == "dE", Mode::Normal);
        return;

      case hash("db"):
      case hash("dB"):
        if (hasBoundaryContext && count == 1) {
          CharRange range = VimCore::wordOperatorRange(
              pos, lines, VimCore::WordOperatorTarget::DeleteBackToWordBegin,
              e == "dB",
              makeWordBoundaryContext(leftColOffset, rightColOffset,
                                      hasLinesAbove, hasLinesBelow));
          if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
              range.end == POSITION_OUTSIDE_BOUNDARY) {
            assert(false && "db/dB has no effect or crosses boundary");
          }

          auto resolved = VimCore::resolveWordOperatorMotion(
              VimCore::WordOperatorTarget::DeleteBackToWordBegin,
              range, pos, lines, leftColOffset);
          VimCore::deleteResolvedRangeAndUpdatePos(
              lines, resolved, pos, Mode::Normal, lineDeleteContext);
          return;
        }
        if (pos.line == 0 && pos.col == 0) {
          // No word before cursor at buffer start; matches Vim's no-op.
          return;
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

          CharRange range = VimCore::wordOperatorRange(
              pos, lines, VimCore::WordOperatorTarget::DeleteBackToWordEnd,
              big,
              makeWordBoundaryContext(leftColOffset, rightColOffset,
                                      hasLinesAbove, hasLinesBelow));
          if (range.begin == POSITION_OUTSIDE_BOUNDARY ||
              range.end == POSITION_OUTSIDE_BOUNDARY) {
            assert(false && "dge/dgE has no effect or crosses boundary");
          }
          auto resolved = VimCore::resolveWordOperatorMotion(
              VimCore::WordOperatorTarget::DeleteBackToWordEnd,
              range, pos, lines, leftColOffset);
          VimCore::deleteResolvedRangeAndUpdatePos(
              lines, resolved, pos, Mode::Normal, lineDeleteContext);
          return;
        }
        if (pos.line == 0 && pos.col == 0) {
          // No word before cursor at buffer start; matches Vim's no-op.
          return;
        }
        deleteBackToWordEnd(lines, pos, count, e == "dgE", Mode::Normal);
        return;

      // dj: linewise delete current line + count lines below.
      // Vim's rule: requires at least 1 line below (j motion); count clamped
      // to lines remaining.
      case hash("dj"):
        {
          if (pos.line >= n - 1) return;  // j motion fails → no-op
          int linesBelow = n - 1 - pos.line;
          int actualCount = std::min(count, linesBelow);
          int endLine = pos.line + actualCount + 1;
          VimCore::deleteLineRangeAndUpdatePos(lines, LineRange(pos.line, endLine), pos, lineDeleteContext);
        }
        return;

      // dk: linewise delete current line + count lines above.
      case hash("dk"):
        {
          if (pos.line == 0) return;  // k motion fails → no-op
          int actualCount = std::min(count, pos.line);
          int beginLine = pos.line - actualCount;
          VimCore::deleteLineRangeAndUpdatePos(lines, LineRange(beginLine, pos.line + 1), pos, lineDeleteContext);
        }
        return;

      // cj: change current line + count lines below (linewise)
      case hash("cj"):
        {
          if (pos.line >= n - 1) return;  // j motion fails → no-op
          int linesBelow = n - 1 - pos.line;
          int actualCount = std::min(count, linesBelow);
          int lastLine = pos.line + actualCount;
          VimCore::linewiseChangeWithAutoindent(lines, pos, mode, pos.line, lastLine, lines[pos.line]);
        }
        return;

      // ck: change current line + count lines above (linewise)
      case hash("ck"):
        {
          if (pos.line == 0) return;  // k motion fails → no-op
          int actualCount = std::min(count, pos.line);
          int beginLine = pos.line - actualCount;
          VimCore::linewiseChangeWithAutoindent(lines, pos, mode, beginLine, pos.line, lines[beginLine]);
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
          int firstNonBlank = VimCore::firstNonBlankColInLine(lines[pos.line]);
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
          char c = line[pos.col];
          bool onWord = big
              ? VimCore::CharMask::isBigWord(c)
              : VimCore::CharMask::isSmallWord(c);

          if (onWord) {
            // On a word: find end of CURRENT word (don't use e motion which goes to next word)
            // Stay on same line, find last char of current word type
            auto isWordChar = [big](char ch) {
              return big
                  ? VimCore::CharMask::isBigWord(ch)
                  : VimCore::CharMask::isSmallWord(ch);
            };
            int endCol = pos.col;
            int lineLen = static_cast<int>(line.size());
            while (endCol + 1 < lineLen && isWordChar(line[endCol + 1])) {
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
              VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
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
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
        }
        mode = Mode::Insert;
        return;

      case hash("c^"):
        if (count > 1) {
          debug("c^: count", count, "ignored (^ motion doesn't use count)");
        }
        {
          int firstNonBlank = VimCore::firstNonBlankColInLine(lines[pos.line]);
          if (firstNonBlank >= pos.col) {
            assert(false && "c^ at or before first non-blank has no effect");
          }
          CharRange r(CursorPos(pos.line, firstNonBlank), CursorPos(pos.line, pos.col));
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
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
          if (r.isEmpty()) return;
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
          mode = Mode::Insert;
        }
        return;

      case hash("D"):
      case hash("d$"):
        // {count}D / {count}d$ spans `count` lines including the cursor line
        // (`:help D`): endLine = pos.line + count - 1. Contrast with {count}dj
        // / {count}dk above, where count is the number of *additional* lines
        // below/above the cursor: endLine = pos.line + count + 1 (exclusive).
        // Same arithmetic shape, opposite count semantics — Vim disagrees with
        // itself across these operators; the asserts above each site state the
        // precondition each one assumes.
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
                    && !VimCore::isParagraphSeparatorLine(lines[goalPos.line]);
          if (goalPos > pos || (atEof && goalPos >= pos)) {
            CharRange range(pos, goalPos);
            if (atEof) {
              // Inclusive at EOF: extend end by one char to make half-open.
              int eofLineLen = static_cast<int>(lines[goalPos.line].size());
              range.end = CursorPos(goalPos.line, std::min(goalPos.col + 1, eofLineLen));
            }
            applyExclusiveMotionEdit(
                lines, pos, mode, range, lineDeleteContext, e[0] == 'c');
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
            applyExclusiveMotionEdit(
                lines, pos, mode, range, lineDeleteContext, e[0] == 'c');
          }
          if (e[0] == 'c') mode = Mode::Insert;
        }
        return;

      // --- Sentence motions ---
      // Both ) and ( are exclusive motions resolved to characterwise or linewise.
      // See VimEditUtils.h for the full rule description and examples.
      case hash("d)"):
      case hash("c)"):
      case hash("d("):
      case hash("c("):
        {
          // Vim's `nv_brace` runs findsent; the motion target may be either
          // before or after the cursor regardless of `)` vs `(` direction
          // (findsent's backward-scan-through-closers can return a backward
          // position even for forward motion). The operator range is just
          // [min, max) — applyExclusiveMotionEdit handles both shapes.
          bool forward = e[1] == ')';
          CursorPos motionPos = pos;
          bool ok = true;
          for (int i = 0; i < count && ok; i++) {
            CursorPos endpoint = VimCore::sentenceOperatorEndpoint(
                motionPos, lines, forward,
                hasBoundaryContext ? (forward ? rightColOffset : leftColOffset) : 0,
                hasBoundaryContext && (forward ? hasLinesBelow : hasLinesAbove));
            if (endpoint == POSITION_OUTSIDE_BOUNDARY) {
              ok = false;
              break;
            }
            motionPos = endpoint;
          }
          if (ok && motionPos != pos) {
            CursorPos lo = motionPos < pos ? motionPos : pos;
            CursorPos hi = motionPos < pos ? pos : motionPos;
            CharRange range(lo, hi);
            applyExclusiveMotionEdit(
                lines, pos, mode, range, lineDeleteContext, e[0] == 'c');
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
        pos.setCol(VimCore::caretTargetColInLine(line, pos.col));
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
        VimCore::WordBoundaryContext boundary;
        if (hasBoundaryContext) {
          boundary = makeWordBoundaryContext(
              leftColOffset, rightColOffset, hasLinesAbove, hasLinesBelow);
        }
        VimCore::WordTextObjectKind kind =
            inner ? VimCore::WordTextObjectKind::Inner
                  : VimCore::WordTextObjectKind::Around;
        r = op == 'c'
            ? VimCore::wordTextObjectChangeRange(
                  pos, linesWrapper, kind, bigWord, boundary)
            : VimCore::wordTextObjectRange(
                  pos, linesWrapper, kind, bigWord, boundary);
      }
      bool isBracketObj = false;
      // Quote objects
      if (obj == '"' || obj == '\'' || obj == '`') {
        r = VimCore::quoteTextObjectRange(
            pos, lines, inner, obj, leftColOffset, rightColOffset);
      }
      // Bracket objects - handle both opening and closing chars
      else if (obj == '(' || obj == ')' || obj == 'b') {
        isBracketObj = true;
        r = VimCore::bracketTextObjectRange(
            pos, lines, inner, '(', ')', leftColOffset, rightColOffset);
      } else if (obj == '{' || obj == '}' || obj == 'B') {
        isBracketObj = true;
        r = VimCore::bracketTextObjectRange(
            pos, lines, inner, '{', '}', leftColOffset, rightColOffset);
      } else if (obj == '[' || obj == ']') {
        isBracketObj = true;
        r = VimCore::bracketTextObjectRange(
            pos, lines, inner, '[', ']', leftColOffset, rightColOffset);
      } else if (obj == '<' || obj == '>') {
        isBracketObj = true;
        r = VimCore::bracketTextObjectRange(
            pos, lines, inner, '<', '>', leftColOffset, rightColOffset);
      } else if (obj == 's') {
        isBracketObj = true;  // route through exclusive-linewise resolver
        r = VimCore::sentenceTextObjectRange(pos, lines, inner);
      } else if (obj == 'p') {
        // Paragraph text object: always linewise. Handle directly here and
        // return — the generic CharRange dispatch below doesn't fit.
        LineRange lineRange = VimCore::paragraphTextObjectRange(
            pos.line, lines, inner);
        if (lineRange.beginLine < 0 || lineRange.endLine < 0 ||
            lineRange.beginLine >= lineRange.endLine) {
          // Vim's current_par FAIL: operator is cleared, cursor unchanged,
          // no mode transition (even for `cap`/`cip`).
          return;
        }
        if (op == 'd') {
          VimCore::deleteLineRangeAndUpdatePos(lines, lineRange, pos);
        } else {
          // c: clear the affected line range, leave one empty line, enter
          // insert at col 0. Mirrors Vim's cc-like linewise change.
          int begin = lineRange.beginLine;
          int end = std::min(lineRange.endLine,
                             static_cast<int>(lines.size()));
          lines.erase(lines.begin() + begin, lines.begin() + end);
          lines.insert(lines.begin() + begin, std::string{});
          pos.line = begin;
          pos.setCol(0);
          mode = Mode::Insert;
        }
        return;
      } else if (!(obj == 'w' || obj == 'W')) {
        // Unknown text object: leave r as default (cursor,cursor) empty;
        // falls through to no-op.
      }

      // Apply operator to range (all ranges are half-open [begin, end)).
      if (r.isValid()) {
        if (r.isEmpty()) {
          // Quote/bracket on empty pair: buffer unchanged, cursor moves to
          // the resolved range start. Matches Vim's current_quote on `i"`
          // over empty quotes.
          pos = r.begin;
          if (op == 'c') mode = Mode::Insert;
        } else if (isBracketObj && r.spansMultiple()) {
          // Multi-line bracket text object may need linewise promotion
          // (op_delete in Vim merges the leading-blanks/full-line case into a
          // linewise delete) or backed-up adjustment (`:help exclusive-linewise`).
          VimCore::ResolvedDeleteRange resolved =
              op == 'c'
                  ? VimCore::resolveExclusiveChangeRange(r, lines)
                  : VimCore::resolveExclusiveDeleteRange(r, lines, true);
          VimCore::deleteResolvedRangeAndUpdatePos(
              lines, resolved, pos, op == 'c' ? Mode::Insert : Mode::Normal);
          if (op == 'c') mode = Mode::Insert;
        } else if (op == 'd') {
          VimCore::deleteRangeAndUpdatePos(lines, r, pos);
        } else {  // op == 'c'
          VimCore::deleteRangeAndUpdatePos(lines, r, pos, Mode::Insert);
          mode = Mode::Insert;
        }
      } else {
        // Invalid range: word text objects can FAIL (Vim's current_word
        // returns FAIL when end_word/fwd_word hit EOF). Vim's clearopbeep
        // leaves cursor at wherever the failed scan ended (clamped) AND
        // does NOT enter insert mode for `c` — the operator is cleared.
        bool wordFailed = false;
        if ((obj == 'w' || obj == 'W') &&
            !(leftColOffset > 0 || rightColOffset > 0 ||
              hasLinesAbove || hasLinesBelow)) {
          bool bigWord = (obj == 'W');
          auto cw = VimCore::currentWord(pos, lines, !inner, bigWord);
          if (!cw.ok) {
            wordFailed = true;
            CursorPos newPos = cw.end;
            if (newPos.line >= 0 && newPos.line < static_cast<int>(lines.size())) {
              int len = static_cast<int>(lines[newPos.line].size());
              if (len == 0) {
                newPos.setCol(0);
              } else if (newPos.col >= len) {
                newPos.setCol(len - 1);
              }
              pos = newPos;
            }
          }
        }
        if (op == 'c' && !wordFailed) {
          // Non-word text objects, or empty range that nonetheless succeeded:
          // still enter insert mode for `c`.
          mode = Mode::Insert;
        }
      }
      return;
    }
  }

  if (mode == Mode::Insert) {
    switch (hash(e)) {
      case hash("<Esc>"):
        // Vim's autoindent strip on `<Esc>` (the `did_ai` path in change.c).
        // If `<CR>` inserted autoindent and nothing user-typed (other than
        // whitespace within the autoindent prefix) has happened since,
        // truncate the line to "". Trip wire: `Verify_InsertNewline`
        // (`i <CR><Esc>` on `[" "]` — autoindent " " on line 1 must strip).
        if (pendingAutoindentLen != nullptr && *pendingAutoindentLen > 0 &&
            pos.line >= 0 && pos.line < static_cast<int>(lines.size())) {
          const std::string& line = lines[pos.line];
          int ai = *pendingAutoindentLen;
          if (static_cast<int>(line.size()) == ai && pos.col == ai) {
            bool allWhitespace = true;
            for (char c : line) {
              if (c != ' ' && c != '\t') { allWhitespace = false; break; }
            }
            if (allWhitespace) {
              lines[pos.line].clear();
              pos.setCol(0);
            }
          }
          *pendingAutoindentLen = 0;
        }
        if (pos.col > 0) pos.setCol(pos.col - 1);
        mode = Mode::Normal;
        return;

      case hash("<BS>"):
        if (pos.col == 0 && pos.line == 0) {
          // Vim: `<BS>` at start of buffer beeps, otherwise no-op. Match that.
          return;
        }
        if (pos.col == 0) {
          // Join with previous line
          int prevLen = static_cast<int>(lines[pos.line - 1].size());
          CursorPos joinPos(pos.line - 1, 0);
          VimCore::joinLines(lines, joinPos, false);
          pos = CursorPos(pos.line - 1, prevLen);
        } else {
          // `smarttab` + `bs=indent`: when cursor sits inside leading
          // whitespace, `<BS>` deletes back to the previous shiftwidth boundary
          // in one press (not one char). Mirrors ChangeGoalHandler's
          // `bsCountForIndent` accounting. Trip wire: `Verify_InsertNewline`
          // (`i<BS>  <BS><Esc>` on `[" ", " ", ""]` — the final `<BS>` must
          // delete the whole `"  "` indent, not one space).
          const std::string& line = lines[pos.line];
          bool inLeadingWhitespace = true;
          for (int c = 0; c < pos.col; c++) {
            if (line[c] != ' ' && line[c] != '\t') {
              inLeadingWhitespace = false;
              break;
            }
          }
          int deleteToCol = pos.col - 1;
          if (inLeadingWhitespace) {
            int sw = VimOptions::shiftwidth();
            deleteToCol = ((pos.col - 1) / sw) * sw;
          }
          CharRange r(CursorPos(pos.line, deleteToCol),
                      CursorPos(pos.line, pos.col));
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

      case hash("<CR>"): {
        // Vim's insert-mode `<CR>` with autoindent (Neovim default):
        //   1. Compute split: `left=[0,col)`, `right=[col,EOL)`.
        //   2. Compute autoindent = leading whitespace of `left`.
        //   3. Strip leading whitespace from `right` (the moved chars).
        //   4. If `did_ai` is still set (pending autoindent from a previous
        //      `<CR>` with no user typing since), TRUNCATE the old line to
        //      "" — Vim's `trunc_line` path in change.c open_line. The
        //      moved-content `right` and the new autoindent are unaffected.
        //   5. New line = autoindent + stripped_right; cursor at column
        //      `len(autoindent)`.
        // Trip wires: `Verify_InsertNewline` (`ib<CR><Esc>` strips moved
        // trailing whitespace; `i<CR><CR><Esc>` truncates the first
        // autoindented line on the second `<CR>`).
        if constexpr (!VimOptions::autoindent()) {
          VimCore::insertText(lines, pos, "\n");
          return;
        }
        const std::string& srcLine = lines[pos.line];
        int splitCol = std::min(pos.col, static_cast<int>(srcLine.size()));
        std::string leftPart = srcLine.substr(0, splitCol);
        std::string rightPart = srcLine.substr(splitCol);
        size_t rightStart = 0;
        while (rightStart < rightPart.size() &&
               (rightPart[rightStart] == ' ' ||
                rightPart[rightStart] == '\t')) {
          rightStart++;
        }
        rightPart.erase(0, rightStart);
        size_t indentLen = 0;
        while (indentLen < leftPart.size() &&
               (leftPart[indentLen] == ' ' ||
                leftPart[indentLen] == '\t')) {
          indentLen++;
        }
        std::string autoindent = leftPart.substr(0, indentLen);
        bool truncateOldLine =
            pendingAutoindentLen != nullptr && *pendingAutoindentLen > 0;
        std::string newLineContent = autoindent + rightPart;
        lines[pos.line] = truncateOldLine ? std::string{} : leftPart;
        lines.insert(lines.begin() + pos.line + 1, newLineContent);
        pos.line += 1;
        pos.setCol(static_cast<int>(autoindent.size()));
        if (pendingAutoindentLen != nullptr) {
          *pendingAutoindentLen = static_cast<int>(autoindent.size());
        }
        return;
      }

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
          while (col > 0 &&
                 VimCore::CharMask::isBlank(ln[col])) {
            col--;
          }
          // Delete word chars backwards
          VimCore::CharMask curr(ln[col]);
          if (curr.smallWord()) {
            while (col > 0 &&
                   VimCore::CharMask::isSmallWord(ln[col - 1])) {
              col--;
            }
          } else if (!curr.blank()) {
            // Non-word, non-blank: delete punctuation sequence
            while (col > 0) {
              VimCore::CharMask prev(ln[col - 1]);
              if (prev.smallWord() || prev.blank()) break;
              col--;
            }
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

expected<vector<ParsedEdit>, EditParseError> parseEdits(string_view seq) {
  string_view sv(seq);
  vector<ParsedEdit> result;
  size_t i = 0;

  while (i < sv.size()) {
    char c = sv[i];

    // Parse optional count prefix
    int cnt = 0;
    if (c >= '1' && c <= '9') {
      constexpr int INT_MAX_VALUE = std::numeric_limits<int>::max();
      while (i < sv.size() && sv[i] >= '0' && sv[i] <= '9') {
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
      return unexpected(EditParseError{
          .kind = EditParseErrorKind::MalformedSpecialKey,
          .offset = i,
      });
    }

    // Handle r{char} - replace with specific character
    if (c == 'r' && i + 1 < sv.size()) {
      if (sv[i + 1] == '<') {
        size_t close = sv.find('>', i + 1);
        if (close != string_view::npos) {
          result.push_back(ParsedEdit{sv.substr(i, close - i + 1), cnt});
          i = close + 1;
          continue;
        }
      }
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
