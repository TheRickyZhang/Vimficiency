#include "VimMotionUtils.h"
#include "CharMask.h"
#include "VimCore.h"
#include "VimEndpointUtils.h"
#include "Types/EdgeType.h"
#include "Types/LineEdgeType.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "Types/CursorPos.h"
#include "Utils/Debug.h"

using namespace std;

namespace VimCore {

// =============================================================================
// Word motions
// =============================================================================
//
// Named motions clamp the shared word scan to Vim cursor landings.
// Edit/text-object ranges use VimEndpointUtils.
//
// =============================================================================

static void motionWord(CursorPos &pos, const Lines &lines,
                       bool forward, EdgeType edgeType, bool big,
                       bool skipCurrent) {
  CursorPos result =
      motionWordCore(pos, lines, forward, edgeType, big, skipCurrent);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    // Vim clamps to buffer edge based on direction
    if (forward) {
      int lastLine = lines.lastLine();
      int lastCol = lines[lastLine].empty()
                        ? 0
                        : static_cast<int>(lines[lastLine].size()) - 1;
      pos = CursorPos(lastLine, lastCol);
    } else {
      pos = CursorPos(0, 0);
    }
  } else {
    pos = result;
  }
}

// Operator-pending variant: when forward word motion would fall off the end
// of the buffer (motionWordCore returns POSITION_OUTSIDE_BOUNDARY), leave the
// cursor at the past-EOL position (col == lines[lastLine].size()) so the
// caller's `isPastEndPosition` check fires and the operator extends through
// the buffer tail. Mirrors Neovim's fwd_word returning FAIL while leaving
// cursor at the NUL/past-EOL position so the operator still applies.
static void motionWordForOperator(CursorPos &pos, const Lines &lines,
                                  bool forward, EdgeType edgeType, bool big,
                                  bool skipCurrent) {
  CursorPos result =
      motionWordCore(pos, lines, forward, edgeType, big, skipCurrent);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    if (forward) {
      int lastLine = lines.lastLine();
      int len = static_cast<int>(lines[lastLine].size());
      pos = CursorPos(lastLine, len);
    } else {
      pos = CursorPos(0, 0);
    }
  } else {
    pos = result;
  }
}

// =============================================================================
// Direct port of Neovim's `fwd_word` (textobject.c) for operator-pending `w`.
// =============================================================================
//
// The non-operator path (motionWordCore + motionW) handles cursor navigation
// where multi-line scans are allowed and the result is a valid normal-mode
// cursor position. The operator path needs different semantics:
//   - When motion would cross a line boundary in eol mode (operator pending),
//     stop on the first line crossing — this implements Vim's "for dw on last
//     word of line, the newline is not included" rule.
//   - When scanning forward through whitespace, STOP at an empty line so the
//     operator only consumes through that empty line (not multiple).
//   - When motion can't advance (cursor on last char of last line), leave
//     cursor at the NUL/past-EOL position so the operator still consumes the
//     buffer tail.
//
// Char classes (matching Neovim's `cls()`):
//   0 - whitespace, NUL, blank chars
//   1 - punctuation (any non-word non-whitespace)
//   2 - word characters (alnum + '_'); collapsed to 1 for bigword `W`/`E`/`B`

namespace {

int wordCharClass(char c, bool big) {
  CharMask m(c);
  if (m.whitespace() || c == '\0') return 0;
  if (big) return 1;  // Bigword: only whitespace vs non-whitespace
  return m.smallWord() ? 2 : 1;
}

int wordCharClassAt(const Lines& lines, CursorPos pos, bool big) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return 0;
  const string& line = lines[pos.line];
  if (pos.col < 0 || pos.col >= static_cast<int>(line.size())) return 0;  // NUL position
  return wordCharClass(line[pos.col], big);
}

// Port of Neovim's `inc`. Returns:
//   -1: cursor was already past end of buffer
//    0: advanced within line (or onto NUL position)
//    1: crossed to start of next line
int wordIncCursor(const Lines& lines, CursorPos& pos) {
  if (lines.empty() || pos.line < 0) return -1;
  if (pos.line >= static_cast<int>(lines.size())) return -1;
  int len = static_cast<int>(lines[pos.line].size());
  if (pos.col < len) {
    pos.col++;
    return 0;
  }
  if (pos.line + 1 >= static_cast<int>(lines.size())) return -1;
  pos.line++;
  pos.col = 0;
  return 1;
}

bool atEmptyLine(const Lines& lines, CursorPos pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return false;
  return pos.col == 0 && lines[pos.line].empty();
}

// Port of `dec` from Neovim's mark.c. Returns:
//   -1: cursor at start of buffer (no movement)
//    0: moved backward within line
//    1: crossed to previous line (cursor at NUL position = col == size, which
//       wordCharClassAt treats as class 0; this matches Vim's coladvance(MAXCOL)
//       semantics where NUL is a visitable position)
int wordDecCursor(const Lines& lines, CursorPos& pos) {
  if (lines.empty() || pos.line < 0) return -1;
  if (pos.col > 0) {
    pos.col--;
    return 0;
  }
  if (pos.line == 0) return -1;
  pos.line--;
  pos.col = static_cast<int>(lines[pos.line].size());  // NUL position
  return 1;
}

// Skip a row of characters of the same class in the given direction.
// Returns true if hit buffer edge (cursor stays at edge).
bool wordSkipChars(int cclass, bool forward, const Lines& lines, CursorPos& pos, bool big) {
  while (wordCharClassAt(lines, pos, big) == cclass) {
    int i = forward ? wordIncCursor(lines, pos) : wordDecCursor(lines, pos);
    if (i == -1) return true;
  }
  return false;
}

}  // namespace (close anonymous so the count-aware motion ports are reachable
   //              from the VimCore::* scope declared in VimMotionUtils.h)

// Port of fwd_word(count, bigword, eol=true) — operator-pending forward `w`.
bool fwdWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    bool lastLine = (pos.line == lines.lastLine());
    int i = wordIncCursor(lines, pos);
    if (i == -1 || (i >= 1 && lastLine)) return false;
    if (i >= 1 && count == 0) return true;

    if (sclass != 0) {
      while (wordCharClassAt(lines, pos, big) == sclass) {
        i = wordIncCursor(lines, pos);
        if (i == -1 || (i >= 1 && count == 0)) return true;
      }
    }

    while (wordCharClassAt(lines, pos, big) == 0) {
      if (atEmptyLine(lines, pos)) break;
      i = wordIncCursor(lines, pos);
      if (i == -1 || (i >= 1 && count == 0)) return true;
    }
  }
  return true;
}

// Port of bck_word(count, bigword, stop=false) — operator-pending backward `b`.
// Vim calls bck_word with stop=false from nv_bck_word for both motion and
// operator-pending use.
bool bckWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  bool stop = false;  // matches nv_bck_word(cap->count1, cap->arg, false)
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (wordDecCursor(lines, pos) == -1) return false;

    if (!stop || sclass == wordCharClassAt(lines, pos, big) || sclass == 0) {
      // Skip whitespace before the word. Stop on empty line.
      while (wordCharClassAt(lines, pos, big) == 0) {
        if (atEmptyLine(lines, pos)) goto finished;
        if (wordDecCursor(lines, pos) == -1) return true;
      }
      // Move backward to start of this word.
      if (wordSkipChars(wordCharClassAt(lines, pos, big), false, lines, pos, big)) return true;
    }
    wordIncCursor(lines, pos);  // overshot — forward one
finished:
    stop = false;
  }
  return true;
}

// Port of end_word(count, bigword, stop=false, empty=false) — `e`/`E`/`de`/`dE`.
// Per Neovim's nv_wordcmd, `flag` (which becomes `stop`) is only true for the
// `cw`/`cW` → `ce`/`cE` mapping. Plain `e`/`E` and operator-pending `de`/`dE`
// use stop=false.
bool endWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (wordIncCursor(lines, pos) == -1) return false;

    if (wordCharClassAt(lines, pos, big) == sclass && sclass != 0) {
      if (wordSkipChars(sclass, true, lines, pos, big)) return false;
    } else {  // stop=false: always enter the "end of next word" branch
      while (wordCharClassAt(lines, pos, big) == 0) {
        if (wordIncCursor(lines, pos) == -1) return false;
      }
      if (wordSkipChars(wordCharClassAt(lines, pos, big), true, lines, pos, big)) return false;
    }
    wordDecCursor(lines, pos);  // overshot — back one
  }
  return true;
}

// Port of bckend_word(count, bigword, eol=false) — `ge`/`gE`. Vim's nv_g_cmd
// calls bckend_word with eol=false always (the early i==1 line-cross return
// inside the function does not fire).
bool bckEndWordOperator(CursorPos& pos, const Lines& lines, bool big, int count) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (wordDecCursor(lines, pos) == -1) return false;

    if (sclass != 0) {
      while (wordCharClassAt(lines, pos, big) == sclass) {
        if (wordDecCursor(lines, pos) == -1) return true;
      }
    }

    while (wordCharClassAt(lines, pos, big) == 0) {
      if (atEmptyLine(lines, pos)) break;
      if (wordDecCursor(lines, pos) == -1) return true;
    }
  }
  return true;
}

namespace {

// Port of `incl` from memline.c: like inc but skips the NUL position at end
// of a non-empty line. Used by current_word.
int wordIncl(const Lines& lines, CursorPos& pos) {
  int r = wordIncCursor(lines, pos);
  // wordIncCursor returns 0 for within-line advance; we need to detect when
  // we landed on the NUL position (col == size) of a non-empty line.
  if (r == 0 && pos.line >= 0 && pos.line < static_cast<int>(lines.size())) {
    int len = static_cast<int>(lines[pos.line].size());
    if (len > 0 && pos.col >= len) {
      // At NUL of non-empty line — Vim's incl skips it.
      return wordIncCursor(lines, pos);
    }
  }
  return r;
}

// Port of `decl` from memline.c: like dec but skips NUL when crossing back
// to a non-empty previous line.
int wordDecl(const Lines& lines, CursorPos& pos) {
  int r = wordDecCursor(lines, pos);
  // wordDecCursor lands at col == size (NUL position) when crossing back.
  // For non-empty prev line, that's col > 0 — Vim's decl re-decs to skip NUL.
  if (r == 1 && pos.col > 0) {
    return wordDecCursor(lines, pos);
  }
  return r;
}

// Port of `back_in_line` from textobject.c: move backward within the current
// line to the start of the current character class run. Does NOT cross lines.
void backInLine(CursorPos& pos, const Lines& lines, bool big) {
  int sclass = wordCharClassAt(lines, pos, big);
  while (pos.col > 0) {
    pos.col--;
    if (wordCharClassAt(lines, pos, big) != sclass) {
      pos.col++;  // undo: we went one past
      return;
    }
  }
}

// Port of `oneleft` from edit.c (non-virtual-edit branch only).
// Returns true if cursor moved (col was > 0), false otherwise.
bool oneLeftCursor(CursorPos& pos) {
  if (pos.col == 0) return false;
  pos.col--;
  return true;
}

// General port of end_word from textobject.c. Parameterized over `stop` and
// `empty`. current_word uses (stop=true, empty=true); plain `e`/`E`/`de`/`dE`
// use (stop=false, empty=false). nv_wordcmd sets stop=true only for `cw`→`ce`.
bool endWord(CursorPos& pos, const Lines& lines, bool big, int count,
             bool stop, bool empty) {
  while (--count >= 0) {
    int sclass = wordCharClassAt(lines, pos, big);
    if (wordIncCursor(lines, pos) == -1) return false;

    bool finished = false;
    if (wordCharClassAt(lines, pos, big) == sclass && sclass != 0) {
      if (wordSkipChars(sclass, true, lines, pos, big)) return false;
    } else if (!stop || sclass == 0) {
      while (wordCharClassAt(lines, pos, big) == 0) {
        if (empty && atEmptyLine(lines, pos)) {
          finished = true;
          break;
        }
        if (wordIncCursor(lines, pos) == -1) return false;
      }
      if (!finished) {
        if (wordSkipChars(wordCharClassAt(lines, pos, big), true, lines, pos, big)) return false;
      }
    }
    if (!finished) wordDecCursor(lines, pos);
    stop = false;
  }
  return true;
}

}  // namespace

// =============================================================================
// Named word motion forwarders
// =============================================================================
//
// Pure motion semantics:
//   w:  Forward  + NextEdge
//   e:  Forward  + WordEdge, skip current first
//   b:  Backward + WordEdge, skip current first
//   ge: Backward + NextEdge, skip current first
//

void motionW(CursorPos &pos, const Lines &lines, bool big) {
  motionWord(pos, lines, true, EdgeType::NextEdge, big, false);
}

void motionB(CursorPos &pos, const Lines &lines, bool big) {
  // For backward direction, End gives edge opposite to travel = leftmost =
  // START skipCurrent needed so b from word start goes to PREVIOUS word start
  motionWord(pos, lines, false, EdgeType::WordEdge, big, true);
}

void motionE(CursorPos &pos, const Lines &lines, bool big) {
  // e needs to skip current position first, otherwise we'd stay at current word
  // end
  motionWord(pos, lines, true, EdgeType::WordEdge, big, true);
}

void motionGe(CursorPos &pos, const Lines &lines, bool big) {
  // For backward direction, Next gives edge in travel direction = rightmost =
  // END skipCurrent needed so ge from word end goes to PREVIOUS word end
  motionWord(pos, lines, false, EdgeType::NextEdge, big, true);
}

void motionWForOperator(CursorPos& pos, const Lines& lines, bool big) {
  // FAIL: cursor stays where fwd_word left it (typically past-EOL on last
  // line). The interpreter's isPastEndPosition / shouldStopAtEndOfLine
  // checks see this and apply the operator through the buffer tail.
  fwdWordOperator(pos, lines, big, 1);
}

void motionEForOperator(CursorPos& pos, const Lines& lines, bool big) {
  endWordOperator(pos, lines, big, 1);
}

void motionBForOperator(CursorPos& pos, const Lines& lines, bool big) {
  bckWordOperator(pos, lines, big, 1);
}

void motionGeForOperator(CursorPos& pos, const Lines& lines, bool big) {
  bckEndWordOperator(pos, lines, big, 1);
}

CurrentWordResult currentWord(CursorPos cursor, const Lines& lines,
                              bool include, bool big) {
  // Port of Vim's `current_word` (textobject.c). Returns the text object
  // span as (start, cursor-at-end, inclusive). The caller converts that
  // inclusive [start, cursor] range to a half-open CharRange.
  //
  // Empty cursor line: Vim's current_word doesn't have an explicit early
  // return for this — it goes through the same path. `back_in_line` is a
  // no-op (col is already 0), startPos = cursor. `(cls() == 0) == include`:
  // empty line has cls=0 (NUL is class 0). For `iw` (include=false), this
  // is `true == false` → false → take the `fwd_word` branch. For `aw`
  // (include=true), it's `true == true` → end_word. We follow Vim's flow
  // and let the algorithm produce whatever Vim's algorithm produces.

  CurrentWordResult result;
  result.ok = true;
  result.inclusive = true;

  // Step 1: Go to start of current word or white space.
  backInLine(cursor, lines, big);
  CursorPos startPos = cursor;

  // Step 2: Find the end of the text object.
  bool curOnWhite = wordCharClassAt(lines, cursor, big) == 0;
  bool includeWhite = false;
  if (curOnWhite == include) {
    // (on white && include white) OR (on word && exclude white): find end of word
    if (!endWord(cursor, lines, big, 1, /*stop=*/true, /*empty=*/true)) {
      // Match Vim's behavior on FAIL: cursor stays where endWord left it.
      result.ok = false;
      result.start = startPos;
      result.end = cursor;
      return result;
    }
  } else {
    // (on word && include white) OR (on white && exclude white): find next word start
    fwdWordOperator(cursor, lines, big, 1);
    // Vim: "If we end up in the first column of the next line (single char
    // word), back up to end of the line."
    if (cursor.col == 0) {
      wordDecl(lines, cursor);
    } else {
      oneLeftCursor(cursor);
    }
    if (include) {
      includeWhite = true;
    }
  }

  // Step 3: include_white extension — for "daw" on last word, extend start
  // backward to include leading whitespace, but NOT indent at line start.
  if (includeWhite &&
      (wordCharClassAt(lines, cursor, big) != 0 ||
       (cursor.col == 0 && !result.inclusive))) {
    CursorPos savedEnd = cursor;
    cursor = startPos;
    if (oneLeftCursor(cursor)) {
      backInLine(cursor, lines, big);
      if (wordCharClassAt(lines, cursor, big) == 0 && cursor.col > 0) {
        startPos = cursor;
      }
    }
    cursor = savedEnd;
  }

  result.start = startPos;
  result.end = cursor;
  return result;
}

// =============================================================================
// Paragraph motion forwarders
// =============================================================================

void motionParagraphPrev(CursorPos &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  pos.line = motionParagraphEndpoint(pos.line, lines, false,
                                                       LineEdgeType::NextEdge);
  pos.setCol(0);
}

void motionParagraphNext(CursorPos &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  int resultLine = motionParagraphEndpoint(
      pos.line, lines, true, LineEdgeType::NextEdge);
  pos.line = resultLine;

  // Special case: if at last line and it's not blank, go to last char
  // (This matches vim's behavior at EOF)
  if (resultLine == n - 1 && !isParagraphSeparatorLine(lines[resultLine])) {
    int lastCol = std::max(0, (int)lines[resultLine].size() - 1);
    pos.setCol(lastCol);
  } else {
    pos.setCol(0);
  }
}

// =============================================================================
// CursorPos helpers
// =============================================================================

int clampCol(const Lines &lines, int col, int lineIdx) {
  int n = static_cast<int>(lines.size());
  assert(lineIdx >= 0 && lineIdx < n);
  int len = static_cast<int>(lines[lineIdx].size());
  if (len == 0)
    return 0;
  return std::clamp(col, 0, len - 1);
}

void moveCol(CursorPos &pos, const Lines &lines, int dx) {
  int nextCol = clampCol(lines, pos.col + dx, pos.line);
  if (nextCol == pos.col) return;
  pos.setCol(nextCol);
}

void moveLine(CursorPos &pos, const Lines &lines, int dy) {
  int n = static_cast<int>(lines.size());
  int nextLine = std::clamp(pos.line + dy, 0, n - 1);
  if (nextLine == pos.line) return;
  pos.line = nextLine;
  // Vertical movement: clamp col to line length but preserve targetCol
  pos.clampColPreservingTarget(clampCol(lines, pos.targetCol, pos.line));
}

// =============================================================================
// Paragraph edge helpers
// =============================================================================

// Move to the "top edge" (start) of the current paragraph.
// If currently on blank lines, goes to first blank line in that run.
void moveToParagraphStart(CursorPos &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);
  pos.line = paragraphStartLine(lines, pos.line);

  // For non-blank paragraph, go to first non-blank column on that line.
  if (!isParagraphSeparatorLine(lines[pos.line]))
    pos.setCol(firstNonBlankColInLine(lines[pos.line]));
  else
    pos.setCol(0);
}

// Move to the "bottom edge" (end) of the current paragraph.
// If currently on blank lines, goes to last blank line in that run.
void moveToParagraphEnd(CursorPos &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);
  pos.line = paragraphEndLine(lines, pos.line);

  // Keep column in bounds.
  pos.setCol(clampCol(lines, pos.col, pos.line));
}

// =============================================================================
// Sentence motions
// =============================================================================

namespace {

struct SentenceScanPos {
  int line = 0;
  int col = 0;
};

bool sameSentenceScanPos(SentenceScanPos a, SentenceScanPos b) {
  return a.line == b.line && a.col == b.col;
}

int sentenceCharAt(const Lines& lines, SentenceScanPos pos) {
  if (lines.empty() || pos.line < 0 || pos.line >= static_cast<int>(lines.size())) {
    return -1;
  }
  const string& line = lines[pos.line];
  if (pos.col < 0) return -1;
  if (pos.col >= static_cast<int>(line.size())) return 0;
  return static_cast<unsigned char>(line[pos.col]);
}

int sentenceInc(const Lines& lines, SentenceScanPos& pos) {
  if (lines.empty() || pos.line < 0 || pos.line >= static_cast<int>(lines.size())) {
    return -1;
  }
  const int len = static_cast<int>(lines[pos.line].size());
  if (pos.col < len) {
    pos.col++;
    return sentenceCharAt(lines, pos) == 0 ? 2 : 0;
  }
  if (pos.line + 1 >= static_cast<int>(lines.size())) {
    return -1;
  }
  pos.line++;
  pos.col = 0;
  return 1;
}

int sentenceIncl(const Lines& lines, SentenceScanPos& pos) {
  int result = sentenceInc(lines, pos);
  if (result >= 1 && pos.col != 0) {
    result = sentenceInc(lines, pos);
  }
  return result;
}

int sentenceDecl(const Lines& lines, SentenceScanPos& pos) {
  if (lines.empty() || pos.line < 0 || pos.line >= static_cast<int>(lines.size())) {
    return -1;
  }
  if (pos.col > 0) {
    pos.col--;
    return sentenceCharAt(lines, pos);
  }
  if (pos.line == 0) {
    return -1;
  }
  pos.line--;
  pos.col = max(0, static_cast<int>(lines[pos.line].size()) - 1);
  return sentenceCharAt(lines, pos);
}

// The sentence scan loop works in `int` (0 = end-of-line, -1 = invalid), so
// these adapters keep sentinel handling separate from character classification.
inline bool sentenceIsWhite(int c) {
  return c >= 0 && CharMask::isWhitespace(static_cast<char>(c));
}

inline bool sentenceIsEndPunct(int c) {
  return c >= 0 && CharMask::isSentenceEnd(static_cast<char>(c));
}

inline bool sentenceIsPunctOrCloser(int c) {
  if (c < 0) return false;
  CharMask cls(static_cast<char>(c));
  return cls.sentenceEnd() || cls.sentenceCloser();
}

bool sentenceInMacro(string_view opt, string_view s) {
  for (size_t i = 0; i < opt.size(); i += 2) {
    char first = opt[i];
    char second = (i + 1 < opt.size()) ? opt[i + 1] : '\0';
    char s0 = s.empty() ? '\0' : s[0];
    char s1 = s.size() < 2 ? '\0' : s[1];

    bool firstMatches = first == s0 || (first == ' ' && (s0 == '\0' || s0 == ' '));
    bool secondMatches = second == s1 ||
        ((second == '\0' || second == ' ') && (s0 == '\0' || s1 == '\0' || s1 == ' '));
    if (firstMatches && secondMatches) return true;
  }
  return false;
}

bool sentenceStartPS(const Lines& lines, int line, int para = 0, bool both = false) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return false;
  const string& s = lines[line];
  char first = s.empty() ? '\0' : s[0];
  if (static_cast<unsigned char>(first) == para || first == '\f' ||
      (both && first == '}')) {
    return true;
  }
  static constexpr string_view SECTIONS = "SHNHH HUnhsh";
  static constexpr string_view PARAGRAPHS = "IPLPPPQPP TPHPLIPpLpItpplpipbp";
  return first == '.' &&
      (sentenceInMacro(SECTIONS, string_view(s).substr(1)) ||
       (para == 0 && sentenceInMacro(PARAGRAPHS, string_view(s).substr(1))));
}

CursorPos sentenceScanToCursor(const Lines& lines, SentenceScanPos pos) {
  if (lines.empty()) return {0, 0};
  pos.line = clamp(pos.line, 0, static_cast<int>(lines.size()) - 1);
  int len = static_cast<int>(lines[pos.line].size());
  if (len == 0) return {pos.line, 0};
  return {pos.line, clamp(pos.col, 0, len - 1)};
}

// Like sentenceScanToCursor but preserves the past-EOL position (col == size)
// that Neovim's findsent leaves on the NUL terminator. Operator endpoints need
// the raw position to construct the exclusive range correctly; cursor display
// needs the clamped variant above.
CursorPos sentenceScanToCursorRaw(const Lines& lines, SentenceScanPos pos) {
  if (lines.empty()) return {0, 0};
  pos.line = clamp(pos.line, 0, static_cast<int>(lines.size()) - 1);
  int len = static_cast<int>(lines[pos.line].size());
  if (len == 0) return {pos.line, 0};
  return {pos.line, clamp(pos.col, 0, len)};
}

// Port of Neovim's `findsent()` from textobject.c — the MOTION path for `)` / `(`.
//
// Looks duplicative of `sentenceExtentAtOrAfter` / `sentenceGapEndpoint` in
// VimEndpointUtils.cpp because both walk over Vim's sentence boundary rules,
// but the two answer different questions and **must stay separate**:
//
//   - findSentenceLikeNeovim → motion: "where does `)` land the cursor?"
//     `)` puts the cursor at the *start* of the next sentence (skipping
//     trailing whitespace forward).
//
//   - sentenceExtentAtOrAfter / sentenceGapEndpoint → operator endpoint:
//     "where does `d)` reach?" `d)` includes the trailing whitespace gap
//     after the sentence-ending punctuation (see :help exclusive-linewise
//     and the gap-after-sentence rule).
//
// Same underlying boundary definition, different endpoint. Forcing a single
// helper would conflate motion and operator semantics — the same kind of
// conflation the project deliberately avoids by separating `motionXxx`
// (NavOptimizer / replay) from `xxxOperatorEndpoint` (TransformOptimizer).
bool findSentenceLikeNeovim(CursorPos& cursor, const Lines& lines, bool forward, int count) {
  if (lines.empty() || count <= 0) return false;

  SentenceScanPos pos{
      clamp(cursor.line, 0, static_cast<int>(lines.size()) - 1),
      cursor.col,
  };
  int len = static_cast<int>(lines[pos.line].size());
  // Allow col up to len (NUL position) — Vim's findsent operates on NUL too,
  // and the sentence text object port (current_sent in VimEndpointUtils) calls
  // findsent with post-incl cursors that legitimately sit at NUL. Out-of-range
  // values are still clamped.
  pos.col = clamp(pos.col, 0, len);

  bool noskip = false;
  while (count > 0) {
    count--;
    SentenceScanPos prevPos = pos;
    int c = sentenceCharAt(lines, pos);

    if (c == 0) {
      do {
        if ((forward ? sentenceIncl(lines, pos) : sentenceDecl(lines, pos)) == -1) {
          break;
        }
      } while (sentenceCharAt(lines, pos) == 0);
      if (forward) {
        goto found;
      }
    } else if (forward && pos.col == 0 && sentenceStartPS(lines, pos.line)) {
      if (pos.line == static_cast<int>(lines.size()) - 1) {
        return false;
      }
      pos.line++;
      pos.col = 0;
      goto found;
    } else if (!forward) {
      sentenceDecl(lines, pos);
    }

    {
      bool foundDot = false;
      while (true) {
        c = sentenceCharAt(lines, pos);
        if (!sentenceIsWhite(c) && !sentenceIsPunctOrCloser(c)) {
          break;
        }

        SentenceScanPos tpos = pos;
        if (sentenceDecl(lines, tpos) == -1 ||
            (lines[tpos.line].empty() && forward)) {
          break;
        }
        if (foundDot) {
          break;
        }
        if (sentenceIsEndPunct(c)) {
          foundDot = true;
        }
        if ((c == ')' || c == ']' || c == '"' || c == '\'') &&
            !sentenceIsPunctOrCloser(sentenceCharAt(lines, tpos))) {
          break;
        }
        sentenceDecl(lines, pos);
      }
    }

    {
      int startLine = pos.line;
      while (true) {
        c = sentenceCharAt(lines, pos);
        if (c == 0 || (pos.col == 0 && sentenceStartPS(lines, pos.line))) {
          if (!forward && pos.line != startLine) {
            pos.line++;
            pos.col = 0;
          }
          break;
        }

        if (sentenceIsEndPunct(c)) {
          SentenceScanPos tpos = pos;
          int next = 0;
          do {
            next = sentenceInc(lines, tpos);
            if (next == -1) break;
          } while (sentenceCharAt(lines, tpos) == ')' ||
                   sentenceCharAt(lines, tpos) == ']' ||
                   sentenceCharAt(lines, tpos) == '"' ||
                   sentenceCharAt(lines, tpos) == '\'');

          int after = sentenceCharAt(lines, tpos);
          if (next == -1 || sentenceIsWhite(after) || after == 0) {
            pos = tpos;
            if (sentenceCharAt(lines, pos) == 0) {
              sentenceIncl(lines, pos);
            }
            break;
          }
        }

        if ((forward ? sentenceIncl(lines, pos) : sentenceDecl(lines, pos)) == -1) {
          if (count > 0) {
            return false;
          }
          noskip = true;
          break;
        }
      }
    }

found:
    while (!noskip && sentenceIsWhite(sentenceCharAt(lines, pos))) {
      if (sentenceIncl(lines, pos) == -1) {
        break;
      }
    }

    if (sameSentenceScanPos(prevPos, pos)) {
      if ((forward ? sentenceIncl(lines, pos) : sentenceDecl(lines, pos)) == -1) {
        if (count > 0) {
          return false;
        }
        break;
      }
      count++;
    }
  }

  cursor = sentenceScanToCursorRaw(lines, pos);
  return true;
}

CursorPos clampCursorToValidLine(CursorPos pos, const Lines& lines) {
  if (lines.empty()) return {0, 0};
  if (pos.line >= static_cast<int>(lines.size())) {
    int lastLine = lines.lastLine();
    int lastLen = static_cast<int>(lines[lastLine].size());
    return {lastLine, lastLen == 0 ? 0 : lastLen - 1};
  }
  int len = static_cast<int>(lines[pos.line].size());
  if (len == 0) return {pos.line, 0};
  return {pos.line, std::min(pos.col, len - 1)};
}

}  // namespace

void motionSentenceNext(CursorPos &pos, const Lines &lines) {
  findSentenceLikeNeovim(pos, lines, true, 1);
  pos = clampCursorToValidLine(pos, lines);
}

void motionSentencePrev(CursorPos &pos, const Lines &lines) {
  findSentenceLikeNeovim(pos, lines, false, 1);
  pos = clampCursorToValidLine(pos, lines);
}

bool findSentenceForOperator(CursorPos& cursor, const Lines& lines, bool forward, int count) {
  return findSentenceLikeNeovim(cursor, lines, forward, count);
}

// =============================================================================
// Character Find (f/F/t/T)
// =============================================================================

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int findCharInLine(char target, string_view line,
                   int startCol, bool forward, bool till) {
  return findCharInLine(target, line, startCol, forward, till,
                        /*count=*/1, /*repeat=*/false);
}

int findCharInLine(char target, string_view line, int startCol,
                   bool forward, bool till, int count, bool repeat) {
  if (count <= 0) return -1;

  const int n = static_cast<int>(line.size());
  const int step = forward ? 1 : -1;
  int i = startCol + step;

  // Repeating t/T skips the adjacent target the prior till stopped beside.
  if (repeat && till && i >= 0 && i < n && line[i] == target) {
    i += step;
    if (count > 1) count--;
  }

  int seen = 0;
  for (; i >= 0 && i < n; i += step) {
    if (line[i] == target) {
      seen++;
      if (seen == count) {
        return till ? i - step : i;
      }
    }
  }
  return -1; // Not found
}

// =============================================================================
// Template instantiations
// =============================================================================

// Return char since f motions are guaranteed to just be one character. Will be
// converted to string further up.
template <bool Forward>
vector<tuple<char, int, int>>
generateFMotions(int currCol, int targetCol,
                 string_view line, int threshold) {
  vector<tuple<char, int, int>> res;
  const int n = static_cast<int>(line.size());

  threshold = min(threshold, abs(currCol - targetCol));
  int l = max(0, targetCol - threshold);
  int r = min(n - 1, targetCol + threshold);

  if constexpr (Forward) {
    l = max(l, currCol + 1);
  } else {
    r = min(r, currCol - 1);
  }
  if (l > r) {
    debug("this shouldn't happen");
    return res;
  }

  res.reserve(r - l + 1);
  // Might be possible to optimize with static instance and resetting only
  // characters touched, but 256 is small so not necessary.
  array<int, 256> cnt{};

  // Count occurrences between cursor and window
  if constexpr (Forward) {
    for (int i = currCol + 1; i < l; i++) {
      cnt[static_cast<unsigned char>(line[i])]++;
    }
    for (int i = l; i <= r; i++) {
      char c = line[i];
      res.emplace_back(c, i, cnt[static_cast<unsigned char>(c)]++);
    }
  } else {
    for (int i = currCol - 1; i > r; i--) {
      cnt[static_cast<unsigned char>(line[i])]++;
    }
    for (int i = r; i >= l; i--) {
      char c = line[i];
      res.emplace_back(c, i, cnt[static_cast<unsigned char>(c)]++);
    }
  }
  return res;
}

template std::vector<std::tuple<char, int, int>>
generateFMotions<true>(int, int, std::string_view, int);

template std::vector<std::tuple<char, int, int>>
generateFMotions<false>(int, int, std::string_view, int);

} // namespace VimCore
