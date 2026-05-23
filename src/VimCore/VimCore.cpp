#include "VimCore.h"

#include <algorithm>
#include <cassert>

#include "CharMask.h"

using namespace std;

namespace VimCore {

// =============================================================================
// 1. String/Line Helpers
// =============================================================================

bool isBlankLine(std::string_view s) {
  for (char c : s) {
    if (!CharMask::isWhitespace(c))
      return false;
  }
  return true; // empty or whitespace-only
}

int firstNonBlankColInLine(std::string_view s) {
  for (int i = 0; i < (int)s.size(); ++i) {
    char c = s[i];
    if (!CharMask::isWhitespace(c))
      return i;
  }
  return 0;
}

// =============================================================================
// 2. CursorPos Stepping
// =============================================================================

// Char at (line,col):
// - Returns 0 only if line is out of range.
// - Returns '\n' if col is outside the actual line (used as "newline/blank"
//   sentinel for word motions).
char getChar(const Lines& lines, int line, int col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return 0;
  const auto& ln = lines[line];
  if (col < 0 || col >= static_cast<int>(ln.size()))
    return '\n';
  return ln[col];
}

// Step forward one character in the logical buffer (across lines).
// Returns false if already at (or past) the last character.
bool stepFwd(const Lines& lines, int& line, int& col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return false;
  const auto& ln = lines[line];
  int len = static_cast<int>(ln.size());
  if (col + 1 < len) {
    ++col;
    return true;
  }
  if (line + 1 < n) {
    ++line;
    col = 0;
    return true;
  }
  return false;
}

// Step backward one character in the logical buffer (across lines).
// Returns false if already at (or before) the first character.
bool stepBack(const Lines& lines, int& line, int& col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return false;
  if (col > 0) {
    --col;
    return true;
  }
  if (line > 0) {
    --line;
    col = static_cast<int>(lines[line].size());
    if (col > 0)
      --col; // last real character of previous line (if any)
    return true;
  }
  return false;
}

// =============================================================================
// 3. Word Motion Core
// =============================================================================

namespace {
// Cold path helper for lineBounded returns
template<bool Forward>
[[gnu::noinline, gnu::cold]]
CursorPos handleLineBounded(CursorPos prev, const Lines& lines) {
  if constexpr (Forward) {
    return CursorPos(prev.line, lines[prev.line].lastCol());
  } else {
    return CursorPos(prev.line, 0);
  }
}
} // anonymous namespace

// Templated version: compile-time dispatch on Forward and Edge
template<bool Forward, EdgeType Edge>
CursorPos motionWordCore(CursorPos pos,
                        const Lines& lines,
                        bool big,
                        bool skipCurrent,
                        bool lineBounded) {
  // ---------------------------------------------------------------------------
  // skipCurrent: Move off current position before searching
  // ---------------------------------------------------------------------------
  if (skipCurrent) {
    CursorPos prevPos = pos;
    CharMask prevClass(lines.get(pos));

    pos = step<Forward>(lines, pos);
    if (pos == prevPos) {
      return POSITION_OUTSIDE_BOUNDARY;
    }

    CharMask currClass(lines.get(pos));

    // b/B (backward WordEdge): Empty line is a word, stop here
    if constexpr (!Forward && Edge == EdgeType::WordEdge) {
      if (currClass.newLine()) {
        return pos;
      }
    }

    // ge/gE (backward NextEdge): Line/word boundaries are stopping points
    if constexpr (!Forward && Edge == EdgeType::NextEdge) {
      bool crossedLine = (pos.line != prevPos.line);
      if ((crossedLine || !sameSpan(prevClass, currClass, big)) &&
          !currClass.blank()) {
        return pos;
      }
    }
    // e/E (forward WordEdge): No early termination
  }

  CharMask c(lines.get(pos));

  // Phase 1: If starting on blank, skip to first non-blank
  if (c.blank()) {
    do {
      CursorPos prev = pos;
      pos = step<Forward>(lines, pos);
      if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;

      if (lineBounded && pos.line != prev.line) {
        return handleLineBounded<Forward>(prev, lines);
      }

      c = CharMask(lines.get(pos));
    } while (c.blank());

    if constexpr (Edge == EdgeType::NextEdge) return pos;
    if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  }

  // Phase 2: Skip current word span
  bool crossedLine = false;
  while (true) {
    CursorPos prev = pos;
    CharMask prevClass = c;
    pos = step<Forward>(lines, pos);
    if (pos == prev) {
      if constexpr (Edge == EdgeType::WordEdge) return prev;
      return POSITION_OUTSIDE_BOUNDARY;
    }
    c = CharMask(lines.get(pos));
    if (pos.line != prev.line) {
      crossedLine = true;
      break;
    }
    if (!continuesSpan(prevClass, c, big)) break;
  }

  if constexpr (Edge == EdgeType::WordEdge) return stepBack<Forward>(lines, pos);

  // Empty line is a word
  if (crossedLine && c.newLine()) {
    if constexpr (Edge == EdgeType::NextEdge) return pos;
    if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  }

  // Phase 3: If at non-blank different type (word only, not bigWord)
  if (!c.blank()) {
    if constexpr (Edge == EdgeType::NextEdge) return pos;
    if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  }

  // Phase 4: Skip whitespace to reach next word (but stop at empty lines)
  while (c.whitespace()) {
    CursorPos prev = pos;
    pos = step<Forward>(lines, pos);
    if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;

    if (lineBounded && pos.line != prev.line) {
      return handleLineBounded<Forward>(prev, lines);
    }

    c = CharMask(lines.get(pos));
    if (c.newLine()) {
      if constexpr (Edge == EdgeType::NextEdge) return pos;
      if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
    }
  }

  if constexpr (Edge == EdgeType::GapEdge) return stepBack<Forward>(lines, pos);
  return pos;
}

// Explicit instantiations (6 combinations used by word motions)
template CursorPos motionWordCore<true, EdgeType::WordEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<true, EdgeType::GapEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<true, EdgeType::NextEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<false, EdgeType::WordEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<false, EdgeType::GapEdge>(CursorPos, const Lines&, bool, bool, bool);
template CursorPos motionWordCore<false, EdgeType::NextEdge>(CursorPos, const Lines&, bool, bool, bool);

// Runtime dispatch version (for compatibility)
CursorPos motionWordCore(CursorPos pos,
                        const Lines& lines,
                        bool forward,
                        EdgeType edge,
                        bool big,
                        bool skipCurrent,
                        bool lineBounded) {
  if (forward) {
    switch (edge) {
      case EdgeType::WordEdge:
        return motionWordCore<true, EdgeType::WordEdge>(pos, lines, big, skipCurrent, lineBounded);
      case EdgeType::GapEdge:
        return motionWordCore<true, EdgeType::GapEdge>(pos, lines, big, skipCurrent, lineBounded);
      case EdgeType::NextEdge:
        return motionWordCore<true, EdgeType::NextEdge>(pos, lines, big, skipCurrent, lineBounded);
      default: __builtin_unreachable();
    }
  } else {
    switch (edge) {
      case EdgeType::WordEdge:
        return motionWordCore<false, EdgeType::WordEdge>(pos, lines, big, skipCurrent, lineBounded);
      case EdgeType::GapEdge:
        return motionWordCore<false, EdgeType::GapEdge>(pos, lines, big, skipCurrent, lineBounded);
      case EdgeType::NextEdge:
        return motionWordCore<false, EdgeType::NextEdge>(pos, lines, big, skipCurrent, lineBounded);
      default: __builtin_unreachable();
    }
  }
}

// =============================================================================
// 4. Paragraph Helpers
// =============================================================================

// Returns the first line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line run.
int paragraphStartLine(const Lines& lines, int lineIdx) {
  int n = (int)lines.size();
  if (n == 0)
    return 0;
  lineIdx = std::clamp(lineIdx, 0, n - 1);

  bool blank = isBlankLine(lines[lineIdx]);
  int i = lineIdx;
  while (i > 0 && isBlankLine(lines[i - 1]) == blank)
    --i;
  return i;
}

// Returns the last line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line run.
int paragraphEndLine(const Lines& lines, int lineIdx) {
  int n = (int)lines.size();
  if (n == 0)
    return 0;
  lineIdx = std::clamp(lineIdx, 0, n - 1);

  bool blank = isBlankLine(lines[lineIdx]);
  int i = lineIdx;
  while (i + 1 < n && isBlankLine(lines[i + 1]) == blank)
    ++i;
  return i;
}

// =============================================================================
// 5. Sentence Helpers
// =============================================================================

// Sentence end at (line,col): . ! ?  then optional closers  then (EOL or space/tab)
bool isSentenceEndAt(const Lines& lines, int line, int col) {
  char c = getChar(lines, line, col);
  if (c == 0)
    return false;
  if (!CharMask::isSentenceEnd(c))
    return false;

  int l = line, k = col;
  while (true) {
    int nl = l, nk = k;
    if (!stepFwd(lines, nl, nk))
      return true; // EOF after punctuation => boundary
    if (nl != l)
      return true; // EOL after punctuation/closers => boundary

    char d = getChar(lines, nl, nk);
    if (CharMask::isSentenceCloser(d)) {
      l = nl;
      k = nk; // consume closer
      continue;
    }
    return d == ' ' || d == '\t'; // boundary only if space/tab
  }
}

// =============================================================================
// skipToSentenceStart: Core helper for sentence motions
// =============================================================================
//
// Given a position at sentence-ending punctuation [.!?], skip past:
// 1. The punctuation mark itself
// 2. Any closers [)'"'\]]  (may cross to next line)
// 3. Whitespace and blank lines
// Returns the first non-blank char position (the next sentence start).
//
// If a blank line is encountered while skipping whitespace, that blank line
// IS the sentence boundary - return its position (line, 0).

std::pair<int, int>
skipToSentenceStart(const Lines& lines, int line, int col) {
  int n = (int)lines.size();
  if (n == 0) return {0, 0};

  int l = line, k = col;

  // Move past the punctuation mark.
  if (!stepFwd(lines, l, k)) {
    return {l, k};  // EOF after punctuation
  }

  // Skip closers; this may cross line boundaries.
  while (true) {
    char c = getChar(lines, l, k);
    if (!CharMask::isSentenceCloser(c)) break;
    if (!stepFwd(lines, l, k)) return {l, k};  // EOF after closers
  }

  // Skip whitespace, stopping on blank-line sentence boundaries.
  while (true) {
    if (l >= n) return {n - 1, 0};

    // Blank line = sentence boundary, stop here
    if (isBlankLine(lines[l])) {
      return {l, 0};
    }

    int len = (int)lines[l].size();
    if (len == 0) {
      return {l, 0};  // Empty line = boundary
    }

    k = std::clamp(k, 0, len - 1);
    char c = lines[l][k];

    if (c == ' ' || c == '\t') {
      if (!stepFwd(lines, l, k)) return {l, k};
      continue;
    }

    // Found non-blank: this is the sentence start
    return {l, k};
  }
}

// =============================================================================
// findCurrentSentenceStart: Find the start of the sentence containing pos
// =============================================================================
//
// For a position in the middle of a sentence, this finds where that sentence
// starts. The algorithm:
// 1. Search backward for the previous sentence boundary
// 2. The current sentence starts at the first non-blank char after that boundary
//
// Special case for blank lines: a blank line is a sentence boundary, so
// from a blank line we find the last sentence of the previous paragraph.

std::pair<int, int>
findCurrentSentenceStart(const Lines& lines, int line, int col) {
  int n = (int)lines.size();
  if (n == 0) return {0, 0};

  line = std::clamp(line, 0, n - 1);
  if ((int)lines[line].size() == 0)
    col = 0;
  else
    col = std::clamp(col, 0, (int)lines[line].size() - 1);

  // If on blank line, we need to find the start of the LAST sentence
  // before the blank line (not the next sentence after).
  if (isBlankLine(lines[line])) {
    // Move to last non-blank line before this blank run
    int prevLine = line - 1;
    while (prevLine >= 0 && isBlankLine(lines[prevLine])) {
      --prevLine;
    }
    if (prevLine < 0) {
      // All blank lines from start - return buffer start
      return {0, 0};
    }

    // Now find the START of the last sentence on prevLine.
    // The last sentence ends at or near the end of prevLine.
    // To find its START, we need to find the PREVIOUS sentence end,
    // then skip forward from there.
    int endCol = (int)lines[prevLine].size() - 1;
    if (endCol < 0) endCol = 0;

    int l = prevLine, k = endCol;

    // Skip backward past any closers/punctuation at the end of line
    // to get into the content of the last sentence
    while (k > 0) {
      char c = getChar(lines, l, k);
      if (CharMask::isSentenceEnd(c) || CharMask::isSentenceCloser(c)) {
        --k;
      } else {
        break;
      }
    }

    // Now search backward for the PREVIOUS sentence end (which marks where
    // the last sentence starts)
    while (true) {
      if (isSentenceEndAt(lines, l, k)) {
        // Found the end of the PREVIOUS sentence - last sentence starts after this
        return skipToSentenceStart(lines, l, k);
      }

      // Check if we hit a blank line
      if (isBlankLine(lines[l])) {
        // Sentence starts at first non-blank after this blank run
        int nextLine = l + 1;
        while (nextLine < n && isBlankLine(lines[nextLine])) ++nextLine;
        if (nextLine >= n) return {n - 1, 0};
        return {nextLine, firstNonBlankColInLine(lines[nextLine])};
      }

      // Step backward
      int pl = l, pk = k;
      if (!stepBack(lines, pl, pk)) {
        // Reached buffer start - sentence starts at first non-blank
        int i = 0;
        while (i < n && isBlankLine(lines[i])) ++i;
        if (i >= n) return {0, 0};
        return {i, firstNonBlankColInLine(lines[i])};
      }

      // Check if stepping back crossed into a blank line
      if (isBlankLine(lines[pl])) {
        // Sentence starts at line l
        return {l, firstNonBlankColInLine(lines[l])};
      }

      l = pl;
      k = pk;
    }
  }

  // Not on blank line - search backward for sentence boundary
  int l = line, k = col;

  // If we're ON sentence-ending punctuation or closer, step back first.
  // We want the start of the sentence we're IN, not the next one.
  {
    char c = getChar(lines, l, k);
    while (CharMask::isSentenceEnd(c) || CharMask::isSentenceCloser(c)) {
      if (!stepBack(lines, l, k)) break;
      c = getChar(lines, l, k);
    }
  }

  // If we're in whitespace, check if it follows a sentence end.
  // If so, we're "between" sentences and should return the start of
  // the sentence that ENDED (not the next one after the whitespace).
  {
    char c = getChar(lines, l, k);
    if (c == ' ' || c == '\t') {
      // Search backward through whitespace to find potential sentence end
      int tl = l, tk = k;
      while (tk > 0) {
        --tk;
        char tc = getChar(lines, tl, tk);
        if (CharMask::isSentenceEnd(tc)) {
          if (isSentenceEndAt(lines, tl, tk)) {
            // We're in whitespace after a sentence end.
            // Find the start of THAT sentence (not the next one).
            // Continue searching backward from the punctuation.
            l = tl;
            k = tk - 1;
            if (k < 0) {
              // At start of line, check previous line
              if (l > 0) {
                --l;
                k = (int)lines[l].size() - 1;
                if (k < 0) k = 0;
              } else {
                // Buffer start - sentence starts at first non-blank
                int i = 0;
                while (i < n && isBlankLine(lines[i])) ++i;
                if (i >= n) return {0, 0};
                return {i, firstNonBlankColInLine(lines[i])};
              }
            }
            break;
          }
        }
        if (!CharMask::isWhitespace(tc) && !CharMask::isSentenceCloser(tc)) {
          break;  // Not in whitespace after sentence
        }
      }
    }
  }

  while (true) {
    if (isSentenceEndAt(lines, l, k)) {
      // Found sentence end - the current sentence starts after this
      return skipToSentenceStart(lines, l, k);
    }

    // Check if we hit a blank line
    if (isBlankLine(lines[l])) {
      // Sentence starts at first non-blank after this blank run
      int nextLine = l + 1;
      while (nextLine < n && isBlankLine(lines[nextLine])) ++nextLine;
      if (nextLine >= n) return {n - 1, 0};
      return {nextLine, firstNonBlankColInLine(lines[nextLine])};
    }

    // Step backward
    int pl = l, pk = k;
    if (!stepBack(lines, pl, pk)) {
      // Reached buffer start - sentence starts at first non-blank
      int i = 0;
      while (i < n && isBlankLine(lines[i])) ++i;
      if (i >= n) return {0, 0};
      return {i, firstNonBlankColInLine(lines[i])};
    }

    // Check if stepping back crossed into a blank line
    if (isBlankLine(lines[pl])) {
      // Sentence starts at line l
      return {l, firstNonBlankColInLine(lines[l])};
    }

    l = pl;
    k = pk;
  }
}

} // namespace VimCore
