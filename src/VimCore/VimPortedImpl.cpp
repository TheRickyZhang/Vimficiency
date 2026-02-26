// VimPortedImpl.cpp
//
// Implementations ported directly from Neovim's C source, with conversions to our types like Line, Position, Range.
// See VimPortedImpl.h for provenance and rationale.

#include "VimPortedImpl.h"
#include "VimCore.h"
#include "VimMotionUtils.h"

#include <algorithm>

namespace VimCore {

namespace {

// Convert Vim-model Pos to valid CursorPos (clamp NUL col to last real char).
inline CursorPos posToCursor(const Lines& lines, Pos p) {
  int n = static_cast<int>(lines.size());
  int l = std::clamp(p.line, 0, n - 1);
  int len = static_cast<int>(lines[l].size());
  int c = len == 0 ? 0 : std::clamp(p.col, 0, len - 1);
  return CursorPos(l, c);
}

// Boundary check helpers.  Return true when the Pos is outside the
// boundary — caller should return POSITION_OUTSIDE_BOUNDARY.
inline bool isOOBForward(Pos p, const Lines& lines,
                         int boundaryOffset, bool hasLinesOutside) {
  int lastLine = static_cast<int>(lines.size()) - 1;
  if (hasLinesOutside && p.line >= lastLine) return true;
  if (boundaryOffset > 0 && p.line == lastLine) {
    int len = static_cast<int>(lines[lastLine].size());
    if (p.col >= len - boundaryOffset) return true;
  }
  return false;
}

inline bool isOOBBackward(Pos p, int boundaryOffset, bool hasLinesOutside) {
  if (hasLinesOutside && p.line <= 0) return true;
  if (boundaryOffset > 0 && p.line == 0 && p.col < boundaryOffset) return true;
  return false;
}

inline bool isOOB(Pos p, const Lines& lines, bool forward,
                  int boundaryOffset, bool hasLinesOutside) {
  return forward ? isOOBForward(p, lines, boundaryOffset, hasLinesOutside)
                 : isOOBBackward(p, boundaryOffset, hasLinesOutside);
}

// ============================================================================
// Core findsent — ported from Neovim search.c / VimMotionUtils.cpp
// ============================================================================
//
// Uses Vim's Pos model where each line has a virtual NUL terminator at
// col == lineLen.  vimGchar returns 0 there.  vimInc/vimDec can land on NUL;
// vimIncl/vimDecl skip it.
//
// 5-step algorithm:
//   1. Handle empty line / paragraph boundary / backward decrement
//   2. Back up past whitespace + sentence punctuation
//   3. Scan for sentence end (.!? + closers + space/NUL)
//   4. Skip whitespace forward
//   5. Retry if position unchanged

static CursorPos findsentImpl(CursorPos cursor, const Lines& lines,
                              bool forward, bool bounded,
                              int boundaryOffset, bool hasLinesOutside) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return cursor;

  int startLine = std::clamp(cursor.line, 0, n - 1);
  int startCol = lines[startLine].empty()
      ? 0
      : std::clamp(cursor.col, 0, static_cast<int>(lines[startLine].size()) - 1);
  Pos cur(startLine, startCol);
  Pos prev = cur;
  bool noskip = false;
  bool foundTarget = false;

  // --- Step 1: empty line / paragraph boundary / backward decrement ---
  unsigned char c = vimGchar(lines, cur);
  if (c == 0) {
    // On empty line — skip in motion direction
    do {
      Pos next = forward ? vimIncl(lines, cur) : vimDecl(lines, cur);
      if (!next.isValid()) break;
      cur = next;
    } while (vimGchar(lines, cur) == 0);
    if (forward) foundTarget = true;
  } else if (forward && cur.col == 0 && isParaBoundaryLine(lines, cur.line)) {
    if (cur.line + 1 >= n) return cursor;
    cur = Pos(cur.line + 1, 0);
    foundTarget = true;
  } else if (!forward) {
    Pos next = vimDecl(lines, cur);
    if (next.isValid()) cur = next;
  }

  if (!foundTarget) {
    // --- Step 2: back up past whitespace + sentence punctuation ---
    {
      bool foundDot = false;
      while (true) {
        c = vimGchar(lines, cur);
        if (c == 0) break;
        if (!isWhitespace(c) && !isSentenceEnd(c) && !isSentenceCloser(c))
          break;

        Pos probe = vimDecl(lines, cur);
        if (!probe.isValid()) break;
        if (forward && lines[probe.line].empty()) break;

        if (foundDot) break;
        if (isSentenceEnd(c)) foundDot = true;

        if (isSentenceCloser(c)) {
          unsigned char tc = vimGchar(lines, probe);
          if (!isSentenceEnd(tc) && !isSentenceCloser(tc) && !isWhitespace(tc))
            break;
        }

        cur = vimDecl(lines, cur);
      }
    }

    // --- Step 3: scan for sentence end ---
    {
      Pos scanStart = cur;
      while (true) {
        c = vimGchar(lines, cur);

        // NUL (empty line) or paragraph boundary at col 0
        if (c == 0 || (cur.col == 0 && isParaBoundaryLine(lines, cur.line))) {
          if (!forward && cur.line != scanStart.line)
            cur = Pos(cur.line + 1, 0);
          break;
        }

        // Sentence-ending punctuation
        if (isSentenceEnd(c)) {
          Pos probe = cur;
          bool bufferEnd = false;
          unsigned char tc;
          // Skip past closers using inc (can land on NUL)
          do {
            Pos next = vimInc(lines, probe);
            if (!next.isValid()) { bufferEnd = true; break; }
            probe = next;
            tc = vimGchar(lines, probe);
          } while (isSentenceCloser(tc));

          if (bufferEnd || isWhitespace(tc) || tc == 0) {
            cur = probe;
            // Skip NUL at EOL to cross to next line
            if (vimGchar(lines, cur) == 0) {
              Pos next = vimInc(lines, cur);
              if (next.isValid()) cur = next;
            }
            break;
          }
        }

        // Advance in scan direction
        Pos next = forward ? vimIncl(lines, cur) : vimDecl(lines, cur);
        if (!next.isValid()) {
          noskip = true;
          break;
        }
        cur = next;
      }
    }
  }

  // --- Step 4: skip whitespace (always forward, using incl) ---
  if (!noskip) {
    while (true) {
      c = vimGchar(lines, cur);
      if (!isWhitespace(c)) break;
      Pos next = vimIncl(lines, cur);
      if (!next.isValid()) break;
      cur = next;
    }
  }

  // --- Step 5: retry if position unchanged ---
  if (cur == prev) {
    Pos next = forward ? vimIncl(lines, cur) : vimDecl(lines, cur);
    if (!next.isValid())
      return posToCursor(lines, cur);
    cur = next;
    CursorPos retry = posToCursor(lines, cur);
    // Recursive retry — boundary check the intermediate position first
    if (bounded && isOOB(cur, lines, forward, boundaryOffset, hasLinesOutside))
      return POSITION_OUTSIDE_BOUNDARY;
    return findsentImpl(retry, lines, forward, bounded,
                        boundaryOffset, hasLinesOutside);
  }

  // Boundary early-break: check the final result
  if (bounded && isOOB(cur, lines, forward, boundaryOffset, hasLinesOutside))
    return POSITION_OUTSIDE_BOUNDARY;

  return posToCursor(lines, cur);
}

// ============================================================================
// current_word — ported from Neovim textobject.c
// ============================================================================
//
// Computes iw/aw/iW/aW text object ranges.  Unlike findsent this does NOT
// use the Pos/NUL model — Vim's current_word only uses inc_cursor/dec_cursor
// (which are real-char-only) and col==0 / oneleft checks.
//
// Character classes (matching Neovim's cls()):
//   0 = whitespace or NUL (end of line)
//   1 = keyword chars (isSmallWordChar)
//   2 = non-keyword, non-whitespace (punctuation)
//   bigword: classes 1 and 2 are merged into 1

// Classify character at a position (returns 0/1/2).
inline int charClass(const Lines& lines, int line, int col, bool bigword) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return 0;
  int len = static_cast<int>(lines[line].size());
  if (col < 0 || col >= len) return 0;  // NUL / off-line → class 0
  unsigned char c = static_cast<unsigned char>(lines[line][col]);
  if (c == ' ' || c == '\t') return 0;
  if (bigword) return 1;
  return isSmallWordChar(c) ? 1 : 2;
}

// back_in_line(): go backward to start of current class run, same line only.
inline void backInLine(const Lines& lines, int& line, int& col, bool bigword) {
  int sclass = charClass(lines, line, col, bigword);
  while (col > 0) {
    if (charClass(lines, line, col - 1, bigword) != sclass) break;
    col--;
  }
}

// incCursor: advance one real char. Returns -1 at EOF, 1 at EOL, 0 normal.
inline int incCursor(const Lines& lines, int& line, int& col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n) return -1;
  int len = static_cast<int>(lines[line].size());
  if (col + 1 < len) { col++; return 0; }
  // At or past last char on line
  if (line + 1 >= n) return -1;  // EOF
  line++; col = 0;
  return 1;  // crossed to next line
}

// decCursor: retreat one real char. Returns -1 at BOF.
inline int decCursor(const Lines& lines, int& line, int& col) {
  if (col > 0) { col--; return 0; }
  if (line <= 0) return -1;  // BOF
  line--;
  int len = static_cast<int>(lines[line].size());
  col = len > 0 ? len - 1 : 0;
  return 0;
}

// fwd_word(count=1, bigword, eol=true): move to start of next word.
inline bool fwdWord(const Lines& lines, int& line, int& col, bool bigword) {
  int sclass = charClass(lines, line, col, bigword);
  int lastLine = static_cast<int>(lines.size()) - 1;
  bool last = (line == lastLine);
  int i = incCursor(lines, line, col);
  if (i == -1) {
    col = static_cast<int>(lines[line].size());  // Vim NUL position
    return false;
  }
  if (i >= 1 && last) return false;
  if (i >= 1) return true;  // started at last char in line, eol=true

  // Skip rest of current word
  if (sclass != 0) {
    while (charClass(lines, line, col, bigword) == sclass) {
      i = incCursor(lines, line, col);
      if (i == -1) {
        col = static_cast<int>(lines[line].size());  // Vim NUL position
        return true;
      }
      if (i >= 1) return true;
    }
  }

  // Skip whitespace
  while (charClass(lines, line, col, bigword) == 0) {
    if (col == 0 && lines[line].empty()) break;  // stop at empty line
    i = incCursor(lines, line, col);
    if (i == -1) {
      col = static_cast<int>(lines[line].size());  // Vim NUL position
      return true;
    }
    if (i >= 1) return true;
  }
  return true;
}

// end_word(count=1, bigword, stop=true, empty=true): move to end of word.
inline bool endWord(const Lines& lines, int& line, int& col, bool bigword) {
  int sclass = charClass(lines, line, col, bigword);
  int i = incCursor(lines, line, col);
  if (i == -1) return false;
  if (i >= 1) goto done;

  if (charClass(lines, line, col, bigword) == sclass && sclass != 0) {
    // In middle of word — skip to end
    while (charClass(lines, line, col, bigword) == sclass) {
      i = incCursor(lines, line, col);
      if (i == -1) return false;
      if (i >= 1) goto done;
    }
  } else {
    // At end of word or on whitespace — skip whitespace, then word
    while (charClass(lines, line, col, bigword) == 0) {
      if (col == 0 && lines[line].empty()) goto done;
      i = incCursor(lines, line, col);
      if (i == -1) return false;
      if (i >= 1) goto done;
    }
    sclass = charClass(lines, line, col, bigword);
    while (charClass(lines, line, col, bigword) == sclass) {
      i = incCursor(lines, line, col);
      if (i == -1) return false;
      if (i >= 1) goto done;
    }
  }
done:
  decCursor(lines, line, col);  // overshot
  return true;
}

// Core current_word for count=1, non-Visual.
static Range currentWordImpl(CursorPos cursor, const Lines& lines,
                             bool include, bool bigword) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(cursor, cursor);

  int startLine = std::clamp(cursor.line, 0, n - 1);
  int startCol = lines[startLine].empty()
      ? 0
      : std::clamp(cursor.col, 0, static_cast<int>(lines[startLine].size()) - 1);

  int sl = startLine, sc = startCol;
  int el = startLine, ec = startCol;
  bool includeWhite = false;

  // Step 1: back_in_line — find start of current class run
  backInLine(lines, sl, sc, bigword);

  int startClass = charClass(lines, sl, sc, bigword);

  // Step 2: depending on class and include flag, find end
  if ((startClass == 0) == include) {
    // (on whitespace && aw) || (on word && iw) → end_word
    el = sl; ec = sc;
    endWord(lines, el, ec, bigword);
    // end is inclusive, convert to half-open (stay on same line)
    int endC = std::min(ec + 1, static_cast<int>(lines[el].size()));
    return Range(CursorPos(sl, sc), CursorPos(el, endC));
  }

  // (on word && aw) || (on whitespace && iw) → fwd_word then back one
  el = sl; ec = sc;
  fwdWord(lines, el, ec, bigword);
  if (ec == 0) {
    // Landed at start of line — back up to end of previous line
    decCursor(lines, el, ec);
  } else {
    ec--;
  }

  if (include) {
    includeWhite = true;
  }

  // Step 3: includeWhite adjustment.
  // If the end position is NOT on whitespace (i.e. the word after cursor
  // has no trailing whitespace that was consumed), try to include leading
  // whitespace before the word instead.  But not indent (col 0).
  if (includeWhite && charClass(lines, el, ec, bigword) != 0) {
    // Move start backward to include whitespace, but don't cross col 0
    int tl = sl, tc = sc;
    if (decCursor(lines, tl, tc) == 0) {
      // Go to start of whitespace run (within same line)
      backInLine(lines, tl, tc, bigword);
      if (charClass(lines, tl, tc, bigword) == 0 && tc > 0) {
        sl = tl;
        sc = tc;
      }
    }
  }

  // Convert inclusive end to half-open (stay on same line)
  int endC = std::min(ec + 1, static_cast<int>(lines[el].size()));
  return Range(CursorPos(sl, sc), CursorPos(el, endC));
}

} // anonymous namespace

// ============================================================================
// Public API
// ============================================================================

CursorPos findsent(CursorPos cursor, const Lines& lines, bool forward) {
  return findsentImpl(cursor, lines, forward,
                      /*bounded=*/false, /*boundaryOffset=*/0,
                      /*hasLinesOutside=*/false);
}

CursorPos findsentBounded(CursorPos cursor, const Lines& lines, bool forward,
                          int boundaryOffset, bool hasLinesOutside) {
  return findsentImpl(cursor, lines, forward,
                      /*bounded=*/true, boundaryOffset, hasLinesOutside);
}

Range currentWord(CursorPos cursor, const Lines& lines,
                  bool include, bool bigword) {
  return currentWordImpl(cursor, lines, include, bigword);
}

Range currentWordBounded(CursorPos cursor, const Lines& lines,
                         bool include, bool bigword,
                         int leftColOffset, int rightColOffset,
                         bool hasLinesAbove, bool hasLinesBelow) {
  Range range = currentWordImpl(cursor, lines, include, bigword);

  int n = static_cast<int>(lines.size());
  if (n == 0) return range;
  int lastLine = n - 1;

  // Check begin against left/top boundary
  if (hasLinesAbove && range.begin.line <= 0) {
    range.begin = POSITION_OUTSIDE_BOUNDARY;
  } else if (leftColOffset > 0 && range.begin.line == 0 &&
             range.begin.col < leftColOffset) {
    range.begin = POSITION_OUTSIDE_BOUNDARY;
  }

  // Check end against right/bottom boundary.
  // range.end is half-open — find last included position for boundary check.
  if (range.end.isValid()) {
    int lastInclLine, lastInclCol;
    if (range.end.col > 0) {
      lastInclLine = range.end.line;
      lastInclCol = range.end.col - 1;
    } else if (range.end.line > 0) {
      lastInclLine = range.end.line - 1;
      int prevLen = static_cast<int>(lines[lastInclLine].size());
      lastInclCol = prevLen > 0 ? prevLen - 1 : 0;
    } else {
      lastInclLine = 0;
      lastInclCol = 0;
    }

    if (hasLinesBelow && lastInclLine >= lastLine) {
      range.end = POSITION_OUTSIDE_BOUNDARY;
    } else if (rightColOffset > 0 && lastInclLine == lastLine) {
      int lineLen = static_cast<int>(lines[lastLine].size());
      if (lastInclCol >= lineLen - rightColOffset) {
        range.end = POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }

  return range;
}

// ============================================================================
// deleteWordForwardRange — dw/dW deletion range (count=1)
// ============================================================================
//
// Computes half-open [begin, end) range using fwdWord() + the
// "don't cross lines" operator adjustment from Neovim's ops.c.

Range deleteWordForwardRange(CursorPos cursor, const Lines& lines, bool big) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(cursor, cursor);

  int sl = std::clamp(cursor.line, 0, n - 1);
  int sc = lines[sl].empty()
      ? 0
      : std::clamp(cursor.col, 0, static_cast<int>(lines[sl].size()) - 1);

  int el = sl, ec = sc;
  bool moved = fwdWord(lines, el, ec, big);

  if (!moved) {
    // At EOF: delete to end of buffer (inclusive of last char)
    int lineLen = static_cast<int>(lines[sl].size());
    if (lineLen > 0 && sc < lineLen) {
      return Range(CursorPos(sl, sc), CursorPos(sl, lineLen));
    }
    return Range(cursor, cursor);  // nothing to delete
  }

  // Don't-cross-lines rule: if w motion crossed lines and current line
  // is non-empty, clamp deletion to end of current line.
  if (el > sl && !lines[sl].empty()) {
    int lineLen = static_cast<int>(lines[sl].size());
    return Range(CursorPos(sl, sc), CursorPos(sl, lineLen));
  }

  // Normal case: exclusive delete to fwdWord destination
  return Range(CursorPos(sl, sc), CursorPos(el, ec));
}

Range deleteWordForwardRangeBounded(CursorPos cursor, const Lines& lines,
                                    bool big, int rightColOffset,
                                    bool hasLinesBelow) {
  Range range = deleteWordForwardRange(cursor, lines, big);

  int n = static_cast<int>(lines.size());
  if (n == 0 || range.begin == range.end) return range;
  int lastLine = n - 1;

  // Check end against right/bottom boundary.
  // range.end is half-open — find last included position for boundary check.
  if (range.end.isValid()) {
    int lastInclLine, lastInclCol;
    if (range.end.col > 0) {
      lastInclLine = range.end.line;
      lastInclCol = range.end.col - 1;
    } else if (range.end.line > 0) {
      lastInclLine = range.end.line - 1;
      int prevLen = static_cast<int>(lines[lastInclLine].size());
      lastInclCol = prevLen > 0 ? prevLen - 1 : 0;
    } else {
      lastInclLine = 0;
      lastInclCol = 0;
    }

    if (hasLinesBelow && lastInclLine >= lastLine) {
      range.end = POSITION_OUTSIDE_BOUNDARY;
    } else if (rightColOffset > 0 && lastInclLine == lastLine) {
      int lineLen = static_cast<int>(lines[lastLine].size());
      if (lastInclCol >= lineLen - rightColOffset) {
        range.end = POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }

  return range;
}

// ============================================================================
// deleteWordBackwardRange — db/dB deletion range (count=1)
// ============================================================================
//
// Ported operator semantics for backward-word delete:
// - motion endpoint is b/B destination
// - deletion is exclusive at cursor
// - special handling at col 0 matches Vim's cross-line behavior

Range deleteWordBackwardRange(CursorPos cursor, const Lines& lines, bool big) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(cursor, cursor);

  int cl = std::clamp(cursor.line, 0, n - 1);
  int cc = lines[cl].empty()
      ? 0
      : std::clamp(cursor.col, 0, static_cast<int>(lines[cl].size()) - 1);
  CursorPos start(cl, cc);

  CursorPos endpoint = start;
  motionB(endpoint, lines, big);
  if (!(endpoint < start)) return Range(start, start);

  // At col 0 crossing lines, Vim includes pure leading indentation that
  // precedes the previous word. If endpoint is the first non-blank char on
  // its line, widen begin to col 0.
  if (start.col == 0 && endpoint.line < start.line && endpoint.col > 0) {
    bool onlyLeadingWs = true;
    for (int i = 0; i < endpoint.col; i++) {
      unsigned char c = static_cast<unsigned char>(lines[endpoint.line][i]);
      if (!isWhitespace(c)) {
        onlyLeadingWs = false;
        break;
      }
    }
    if (onlyLeadingWs) endpoint.setCol(0);
  }

  CursorPos end = POSITION_OUTSIDE_BOUNDARY;
  if (start.col > 0) {
    // Normal exclusive-at-cursor case.
    end = start;
  } else if (endpoint.line < start.line) {
    // At col 0 crossing lines:
    // - previous non-empty line: delete up to previous line end
    // - previous empty line: delete separator newline by ending at cursor
    int prevLine = start.line - 1;
    int prevLen = static_cast<int>(lines[prevLine].size());
    end = (prevLen > 0) ? CursorPos(prevLine, prevLen) : start;
  }

  if (!end.isValid() || !(endpoint < end)) return Range(start, start);
  return Range(endpoint, end);
}

Range deleteWordBackwardRangeBounded(CursorPos cursor, const Lines& lines,
                                     bool big, int leftColOffset,
                                     bool hasLinesAbove) {
  Range range = deleteWordBackwardRange(cursor, lines, big);
  if (range.begin == range.end) return range;

  if (hasLinesAbove && range.begin.line <= 0) {
    range.begin = POSITION_OUTSIDE_BOUNDARY;
    return range;
  }
  if (leftColOffset > 0 && range.begin.line == 0 &&
      range.begin.col < leftColOffset) {
    range.begin = POSITION_OUTSIDE_BOUNDARY;
    return range;
  }

  return range;
}

} // namespace VimCore
