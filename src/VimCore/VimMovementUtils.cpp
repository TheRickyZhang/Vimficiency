#include "VimCore/EdgeType.h"
#include "VimUtils.h"
#include "Utils/SentinelChar.h"
#include "VimMovementUtils.h"

#include <algorithm>
#include <cassert>
#include <array>

#include "Editor/Position.h"
#include "Utils/Debug.h"

using namespace std;
using namespace VimUtils;

// =============================================================================
// Word motions - general interface
// =============================================================================
//
// Unified word motion based on (direction, edgeType, isWORD).
//
// EdgeType is DIRECTION-INDEPENDENT - it describes which edge we seek:
//   WordEdge: the edge of the word we traverse (step back into the word)
//   GapEdge:  the edge of the gap before next word (step back into gap)
//   NextEdge: the edge of the next unit (stay at first char of next thing)
//
// For MOTIONS (cursor movement):
//   w  = Forward  + NextEdge  (to start of next word)
//   e  = Forward  + WordEdge  (to end of word)
//   b  = Backward + WordEdge  (to start of word)
//   ge = Backward + NextEdge  (to end of previous word)
//
// For DELETIONS (different edges for some):
//   dw  = Forward  + GapEdge   (delete including trailing whitespace)
//   de  = Forward  + WordEdge
//   db  = Backward + WordEdge
//   dge = Backward + NextEdge
//
// =============================================================================

namespace {

// =============================================================================
// Direction-agnostic step helpers
// =============================================================================

// Step including empty lines (for word motions where empty line = word)
inline Position step(const Lines& lines, Position pos, bool forward) {
    return forward ? lines.getNextPosIncludeEmpty(pos) : lines.getPrevPosIncludeEmpty(pos);
}

inline Position stepBack(const Lines& lines, Position pos, bool forward) {
    return forward ? lines.getPrevPosIncludeEmpty(pos) : lines.getNextPosIncludeEmpty(pos);
}

// Check if position has reached or passed target in the given direction
inline bool reachedTarget(Position pos, Position target, bool forward) {
    if (forward) {
        return pos.line > target.line ||
               (pos.line == target.line && pos.col >= target.col);
    } else {
        return pos.line < target.line ||
               (pos.line == target.line && pos.col <= target.col);
    }
}

// =============================================================================
// Unified word motion core
// =============================================================================
//
// Handles both word and WORD motions in both directions.
// The `big` parameter controls character classification:
//   big=true (WORD):  non-blank chars are all "same type"
//   big=false (word): keyword chars vs symbol chars are different types
//

void motionWordCore(Position& pos, const Lines& lines, bool forward,
                    EdgeType edge, bool big) {
    unsigned char c = lines.get(pos);

    // Phase 1: If starting on blank, skip to first non-blank
    if (isBlank(c)) {
        do {
            Position prev = pos;
            pos = step(lines, pos, forward);
            if (pos == prev) return;  // Can't move (at buffer boundary)
            c = lines.get(pos);
        } while (isBlank(c));

        if (edge == EdgeType::NextEdge) return;  // First non-blank = next word start
        if (edge == EdgeType::GapEdge) {
            pos = stepBack(lines, pos, forward);  // Last blank before next word
            return;
        }
        // End: continue to find word end below
    }

    // Phase 2: Skip same-type chars (current word)
    // For WORD: all non-blank are same type
    // For word: keyword vs symbol are different types
    bool startIsWordChar = isSmallWordChar(c);
    bool crossedLine = false;
    do {
        Position prev = pos;
        pos = step(lines, pos, forward);
        if (pos == prev) return;  // Hit buffer boundary
        c = lines.get(pos);
        // Line boundary = word boundary (newline terminates WORD)
        if (pos.line != prev.line) {
            crossedLine = true;
            break;
        }
    } while (!isBlank(c) && (big || isSmallWordChar(c) == startIsWordChar));

    if (edge == EdgeType::WordEdge) {
        pos = stepBack(lines, pos, forward);  // Last char of word
        return;
    }

    // Empty line is a word (vim doc: "An empty line is also considered to be a word")
    // c == '\n' means we landed on an empty line
    if (crossedLine && c == '\n') {
        if (edge == EdgeType::NextEdge) return;  // Empty line is the next word
        // For GapEdge: empty line ends the gap
        if (edge == EdgeType::GapEdge) {
            pos = stepBack(lines, pos, forward);
            return;
        }
    }

    // Phase 3: If at non-blank different type (word only, not WORD)
    // This happens when we hit keyword->symbol or symbol->keyword boundary
    if (!isBlank(c)) {
        // At start of adjacent word (different type)
        if (edge == EdgeType::NextEdge) return;
        if (edge == EdgeType::GapEdge) {
            pos = stepBack(lines, pos, forward);  // Char before this word
            return;
        }
    }

    // Phase 4: Skip whitespace to reach next word (but stop at empty lines)
    while (isWhitespace(c)) {
        Position prev = pos;
        pos = step(lines, pos, forward);
        if (pos == prev) return;  // Hit buffer boundary
        c = lines.get(pos);
        // Empty line is a word, stop here
        if (c == '\n') {
            if (edge == EdgeType::NextEdge) return;
            if (edge == EdgeType::GapEdge) {
                pos = stepBack(lines, pos, forward);
                return;
            }
        }
    }

    if (edge == EdgeType::GapEdge) {
        pos = stepBack(lines, pos, forward);  // Last blank before next word
    }
    // Next: already at next word start
}

// =============================================================================
// Reach-checking version (returns bool, allows early exit)
// =============================================================================
//
// Same algorithm as motionWordCore, but:
// 1. Takes pos by value (doesn't modify caller's position)
// 2. Checks if motion would reach targetPos
// 3. Returns true immediately upon reaching target
//

bool checkMotionWordReachesCore(Position pos, const Position& targetPos,
                                const Lines& lines, bool forward,
                                EdgeType edge, bool big) {
    unsigned char c = lines.get(pos);

    // For WordEdge and GapEdge, we step past and then step back.
    // Don't early-return during stepping phases for these edge types,
    // only check final position after stepBack.
    bool canEarlyReturn = (edge == EdgeType::NextEdge);

    // Phase 1: If starting on blank, skip to first non-blank
    if (isBlank(c)) {
        do {
            Position prev = pos;
            pos = step(lines, pos, forward);
            if (pos == prev) return reachedTarget(pos, targetPos, forward);
            if (canEarlyReturn && reachedTarget(pos, targetPos, forward)) return true;
            c = lines.get(pos);
        } while (isBlank(c));

        if (edge == EdgeType::NextEdge) return reachedTarget(pos, targetPos, forward);
        if (edge == EdgeType::GapEdge) {
            pos = stepBack(lines, pos, forward);
            return reachedTarget(pos, targetPos, forward);
        }
    }

    // Phase 2: Skip same-type chars (current word)
    bool startIsWordChar = isSmallWordChar(c);
    do {
        Position prev = pos;
        pos = step(lines, pos, forward);
        if (pos == prev) return reachedTarget(pos, targetPos, forward);
        if (canEarlyReturn && reachedTarget(pos, targetPos, forward)) return true;
        c = lines.get(pos);
        // Line boundary = word boundary (newline terminates WORD)
        if (pos.line != prev.line) break;
    } while (!isBlank(c) && (big || isSmallWordChar(c) == startIsWordChar));

    if (edge == EdgeType::WordEdge) {
        pos = stepBack(lines, pos, forward);
        return reachedTarget(pos, targetPos, forward);
    }

    // Phase 3: If at non-blank different type (word only)
    if (!isBlank(c)) {
        if (edge == EdgeType::NextEdge) return reachedTarget(pos, targetPos, forward);
        if (edge == EdgeType::GapEdge) {
            pos = stepBack(lines, pos, forward);
            return reachedTarget(pos, targetPos, forward);
        }
    }

    // Phase 4: Skip blanks to reach next word
    while (isBlank(c)) {
        Position prev = pos;
        pos = step(lines, pos, forward);
        if (pos == prev) return reachedTarget(pos, targetPos, forward);
        if (canEarlyReturn && reachedTarget(pos, targetPos, forward)) return true;
        c = lines.get(pos);
    }

    if (edge == EdgeType::GapEdge) pos = stepBack(lines, pos, forward);
    return reachedTarget(pos, targetPos, forward);
}

} // anonymous namespace

void VimMovementUtils::motionWord(Position &pos,
                                   const Lines &lines,
                                   bool forward,
                                   EdgeType edgeType,
                                   bool big,
                                   bool skipCurrent) {
  if (skipCurrent) {
    unsigned char prevChar = lines.get(pos);
    pos = step(lines, pos, forward);
    unsigned char currChar = lines.get(pos);

    // Empty line is a word - if we landed on one, handle it
    if (currChar == '\n') {
      // For backward + WordEdge (b/B): empty line IS the word start, stop here
      if (!forward && edgeType == EdgeType::WordEdge) {
        return;
      }
      // For forward + NextEdge (w/W): empty line IS the next word, stop here
      if (forward && edgeType == EdgeType::NextEdge) {
        return;
      }
    }

    // For backward + NextEdge (ge/gE), if we crossed a word boundary (landed on blank or
    // different word type), we're at the word end we're seeking. Return immediately.
    if (!forward && edgeType == EdgeType::NextEdge) {
      bool crossedBoundary = isBlank(currChar) ||
                             isBlank(prevChar) ||
                             (!big && isSmallWordChar(currChar) != isSmallWordChar(prevChar));
      if (crossedBoundary && !isBlank(currChar)) {
        return;
      }
    }
  }
  motionWordCore(pos, lines, forward, edgeType, big);
}

bool VimMovementUtils::checkMotionWordReaches(Position pos,
                                              const Position& boundaryPos,
                                              const Lines& lines,
                                              bool forward,
                                              EdgeType edgeType,
                                              bool big,
                                              bool skipCurrent) {
  if (skipCurrent) {
    Position posAfterSkip = step(lines, pos, forward);

    unsigned char prevChar = lines.get(pos);
    pos = posAfterSkip;
    unsigned char currChar = lines.get(pos);

    // For backward + NextEdge, if we crossed a word boundary, we're at word end.
    if (!forward && edgeType == EdgeType::NextEdge) {
      bool crossedBoundary = isBlank(currChar) ||
                             isBlank(prevChar) ||
                             (!big && isSmallWordChar(currChar) != isSmallWordChar(prevChar));
      if (crossedBoundary && !isBlank(currChar)) {
        return reachedTarget(pos, boundaryPos, forward);
      }
    }
  }
  return checkMotionWordReachesCore(pos, boundaryPos, lines, forward, edgeType, big);
}

bool VimMovementUtils::checkMotionWordReachesCharTableMatching(
    Position cursor,
    const Position& editRegionEnd,
    CharType boundaryCharType,
    const Lines& lines,
    bool forward,
    EdgeType edgeType,
    bool big,
    bool skipCurrent) {
  // Run motion to find endpoint
  Position endPos = cursor;
  motionWord(endPos, lines, forward, edgeType, big, skipCurrent);

  // Check if endpoint reached the edit region edge
  bool reachedEdge = forward
    ? (endPos >= editRegionEnd)
    : (endPos <= editRegionEnd);

  if (!reachedEdge) {
    return false;  // Didn't reach edge, safe
  }

  // At edge - use crossing table to determine if we'd cross
  CharType edgeCharType = getCharType(lines.get(endPos));

  // Select crossing function based on edge type and WORD mode
  if (big) {
    switch (edgeType) {
      case EdgeType::WordEdge: return canEndCrossWORD(edgeCharType, boundaryCharType);
      case EdgeType::GapEdge:  return canSpaceCrossWORD(edgeCharType, boundaryCharType);
      case EdgeType::NextEdge: return canNextCrossWORD(edgeCharType, boundaryCharType);
      default: return true;
    }
  } else {
    switch (edgeType) {
      case EdgeType::WordEdge: return canEndCross(edgeCharType, boundaryCharType);
      case EdgeType::GapEdge:  return canSpaceCross(edgeCharType, boundaryCharType);
      case EdgeType::NextEdge: return canNextCross(edgeCharType, boundaryCharType);
      default: return true;
    }
  }
}

// =============================================================================
// Named word motion forwarders
// =============================================================================
//
// Pure motion semantics:
//   w: Forward + Next (to next word start)
//   e: Forward + End (to word end), skip current first
//   b: Backward + Next (to previous word start)
//   ge: Backward + End (to previous word end), skip current first
//

void VimMovementUtils::motionW(Position &pos, const Lines &lines, bool big) {
  motionWord(pos, lines, true, EdgeType::NextEdge, big, false);
}

void VimMovementUtils::motionB(Position &pos, const Lines &lines, bool big) {
  // For backward direction, End gives edge opposite to travel = leftmost = START
  // skipCurrent needed so b from word start goes to PREVIOUS word start
  motionWord(pos, lines, false, EdgeType::WordEdge, big, true);
}

void VimMovementUtils::motionE(Position &pos, const Lines &lines, bool big) {
  // e needs to skip current position first, otherwise we'd stay at current word end
  motionWord(pos, lines, true, EdgeType::WordEdge, big, true);
}

void VimMovementUtils::motionGe(Position &pos, const Lines &lines, bool big) {
  // For backward direction, Next gives edge in travel direction = rightmost = END
  // skipCurrent needed so ge from word end goes to PREVIOUS word end
  motionWord(pos, lines, false, EdgeType::NextEdge, big, true);
}

// =============================================================================
// Named forwarders - handle +1 shift where needed
// =============================================================================
//
// From boundary-logic.md:
//   de: Current Char + (Forward, End) from NEXT char
//   db: Current Char + (Backward, End) from NEXT char
//
// For pure motions:
//   e: step forward first (so we find next word end, not stay at current)
//   ge: step backward first (so we find previous word end, not stay at current)
//   w/b: no shift needed (implementation already skips current position)
//

void VimMovementUtils::motionParagraphPrev(Position &pos,
                                   const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);

  // If currently on blank lines, skip past them first
  while (pos.line > 0 && isBlankLineStr(lines[pos.line])) {
    pos.line--;
  }
  // Now scan backward for the previous blank line
  int i = pos.line - 1;
  while (i >= 0 && !isBlankLineStr(lines[i])) {
    i--;
  }
  pos.line = max(i, 0);
  pos.setCol(0);
}

void VimMovementUtils::motionParagraphNext(Position &pos,
                                   const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);

  // If currently on blank lines, skip past them first
  while (pos.line < n && isBlankLineStr(lines[pos.line])) {
    pos.line++;
  }
  if (pos.line >= n) {
    pos.line = n - 1;
    pos.setCol(0);
    return;
  }
  // Now scan forward for the next blank line
  int i = pos.line + 1;
  while (i < n && !isBlankLineStr(lines[i])) {
    i++;
  }

  if (i < n) {
    // Found a blank line - go to it at column 0
    pos.line = i;
    pos.setCol(0);
  } else {
    // No blank line found - go to last character of last line
    pos.line = n - 1;
    int lastCol = std::max(0, (int)lines[pos.line].size() - 1);
    pos.setCol(lastCol);
  }
}

static bool isSentenceCloser(unsigned char c) {
  return c == ')' || c == ']' || c == '"' || c == '\'';
}

// Sentence end at (line,col): . ! ?  then optional closers  then (EOL or
// space/tab)
static bool isSentenceEndAt(const Lines &lines, int line,
                            int col) {
  unsigned char c = getChar(lines, line, col);
  if (c == 0)
    return false;
  if (c != '.' && c != '!' && c != '?')
    return false;

  int l = line, k = col;
  while (true) {
    int nl = l, nk = k;
    if (!stepFwd(lines, nl, nk))
      return true; // EOF after punctuation => boundary
    if (nl != l)
      return true; // EOL after punctuation/closers => boundary

    unsigned char d = getChar(lines, nl, nk);
    if (isSentenceCloser(d)) {
      l = nl;
      k = nk; // consume closer
      continue;
    }
    return d == ' ' || d == '\t'; // boundary only if space/tab
  }
}

static std::pair<int, int>
findSentenceStart(const Lines &lines, int line, int col) {
  int n = (int)lines.size();
  if (n == 0)
    return {0, 0};

  line = std::clamp(line, 0, n - 1);
  if ((int)lines[line].size() == 0)
    col = 0;
  else
    col = std::clamp(col, 0, (int)lines[line].size() - 1);

  // If on blank line run, move up to last nonblank char before it (if any).
  while (line > 0 && isBlankLineStr(lines[line])) {
    --line;
    col = (int)lines[line].size();
    if (col > 0)
      --col;
  }

  int l = line, k = col;

  while (true) {
    if (isSentenceEndAt(lines, l, k)) {
      int sl = l, sk = k;
      if (!stepFwd(lines, sl, sk))
        return {l, k};

      // skip closers (same line only)
      while (true) {
        unsigned char c = getChar(lines, sl, sk);
        if (!isSentenceCloser(c))
          break;
        int tl = sl, tk = sk;
        if (!stepFwd(lines, tl, tk))
          break;
        if (tl != sl)
          break;
        sl = tl;
        sk = tk;
      }

      // skip spaces/tabs and blank lines
      while (true) {
        if (sl >= n)
          return {n - 1, 0};
        if (isBlankLineStr(lines[sl])) {
          ++sl;
          sk = 0;
          continue;
        }
        int len = (int)lines[sl].size();
        if (len == 0) {
          ++sl;
          sk = 0;
          continue;
        }
        sk = std::clamp(sk, 0, len - 1);
        unsigned char c = (unsigned char)lines[sl][sk];
        if (c == ' ' || c == '\t') {
          if (!stepFwd(lines, sl, sk))
            break;
          continue;
        }
        break;
      }

      if (sl >= n)
        return {n - 1, 0};
      return {sl, sk}; // FIX: Use sk, not firstNonBlankColInLineStr
    }

    int pl = l, pk = k;
    if (!stepBack(lines, pl, pk))
      break;
    l = pl;
    k = pk;

    if (isBlankLineStr(lines[l])) {
      while (l < n && isBlankLineStr(lines[l]))
        ++l;
      if (l >= n)
        return {n - 1, 0};
      return {l, firstNonBlankColInLineStr(
                     lines[l])}; // This one is OK - starting fresh line
    }
  }

  int i = 0;
  while (i < n && isBlankLineStr(lines[i]))
    ++i;
  if (i >= n)
    return {n - 1, 0};
  return {i, firstNonBlankColInLineStr(
                 lines[i])}; // This one is OK - starting fresh line
}




/*
 * ------------------------------ BEGIN VimUtils ------------------------------
 */

// Fundamental helpers for working with position
int VimMovementUtils::clampCol(const Lines &lines, int col,
                       int lineIdx) {
  int n = static_cast<int>(lines.size());
  assert(lineIdx >= 0 && lineIdx < n);
  int len = static_cast<int>(lines[lineIdx].size());
  if (len == 0)
    return 0;
  return std::clamp(col, 0, len - 1);
}

void VimMovementUtils::moveCol(Position &pos, const Lines &lines,
                       int dx) {
  pos.setCol(clampCol(lines, pos.col + dx, pos.line));
}

void VimMovementUtils::moveLine(Position &pos, const Lines &lines,
                        int dy) {
  int n = static_cast<int>(lines.size());
  pos.line = std::clamp(pos.line + dy, 0, n - 1);
  pos.col = clampCol(lines, pos.targetCol, pos.line);
}

// Move to the "top edge" (start) of the current paragraph.
// If currently on blank lines, goes to first blank line in that run.
void VimMovementUtils::moveToParagraphStart(Position &pos,
                                    const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);
  pos.line = paragraphStartLine(lines, pos.line);

  // For non-blank paragraph, go to first non-blank column on that line.
  if (!isBlankLineStr(lines[pos.line]))
    pos.setCol(firstNonBlankColInLineStr(lines[pos.line]));
  else
    pos.setCol(0);
}

// Move to the "bottom edge" (end) of the current paragraph.
// If currently on blank lines, goes to last blank line in that run.
void VimMovementUtils::moveToParagraphEnd(Position &pos,
                                  const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);
  pos.line = paragraphEndLine(lines, pos.line);

  // Keep column in bounds.
  pos.setCol(clampCol(lines, pos.col, pos.line));
}

void VimMovementUtils::motionSentenceNext(Position &pos,
                                  const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  int line = std::clamp(pos.line, 0, n - 1);
  int col = (int)lines[line].size() == 0
                ? 0
                : std::clamp(pos.col, 0, (int)lines[line].size() - 1);

  // If currently on blank run: jump to next nonblank line start.
  if (isBlankLineStr(lines[line])) {
    while (line < n && isBlankLineStr(lines[line]))
      ++line;
    if (line >= n)
      return;
    pos.line = line;
    pos.setCol(firstNonBlankColInLineStr(lines[line]));
    return;
  }

  int l = line, k = col;
  while (true) {
    if (isSentenceEndAt(lines, l, k)) {
      // move past the sentence-ending punctuation
      if (!stepFwd(lines, l, k))
        return;

      // skip closers (same line only)
      while (true) {
        unsigned char c = getChar(lines, l, k);
        if (!isSentenceCloser(c))
          break;
        int tl = l, tk = k;
        if (!stepFwd(lines, tl, tk))
          break;
        if (tl != l)
          break;
        l = tl;
        k = tk;
      }

      // skip spaces/tabs and blank lines
      while (true) {
        if (l >= n)
          return;
        if (isBlankLineStr(lines[l])) {
          ++l;
          k = 0;
          continue;
        }
        int len = (int)lines[l].size();
        if (len == 0) {
          ++l;
          k = 0;
          continue;
        }
        k = std::clamp(k, 0, len - 1);
        unsigned char c = (unsigned char)lines[l][k];
        if (c == ' ' || c == '\t') {
          if (!stepFwd(lines, l, k))
            return;
          continue;
        }
        break;
      }

      if (l >= n)
        return;
      pos.line = l;
      pos.setCol(k);
      return;
    }

    if (!stepFwd(lines, l, k))
      return;
  }
}

void VimMovementUtils::motionSentencePrev(Position &pos,
                                  const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0) return;

  auto [sl, sc] = findSentenceStart(lines, pos.line, pos.col);

  // If already at sentence start, go to previous sentence start.
  if (sl == pos.line && sc == pos.col) {
    int l = sl, k = sc;
    if (stepBack(lines, l, k)) {
      auto [psl, psc] = findSentenceStart(lines, l, k);
      pos.line = psl;
      pos.setCol(psc);
      return;
    }
  }

  pos.line = sl;
  pos.setCol(sc);
}


// -------------------- Character Find (f/F/t/T) --------------------

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int VimMovementUtils::findCharInLine(char target, const string& line, int startCol, bool forward, bool till) {
  const int n = static_cast<int>(line.size());

  if (forward) {
    for (int i = startCol + 1; i < n; i++) {
      if (line[i] == target) {
        return till ? i - 1 : i;
      }
    }
  } else {
    for (int i = startCol - 1; i >= 0; i--) {
      if (line[i] == target) {
        return till ? i + 1 : i;
      }
    }
  }
  return -1; // Not found
}


// -------------------- Templates -------------------- 

// Return char since f motions are guaranteed to just be one character. Will be converted to string further up.
template <bool Forward>
vector<tuple<char, int, int>> VimMovementUtils::generateFMotions(int currCol, int targetCol, const string &line, int threshold) {
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
      cnt[line[i]]++;
    }
    for (int i = l; i <= r; i++) {
      char c = line[i];
      res.emplace_back(c, i, cnt[c]++);
    }
  } else {
    for (int i = currCol - 1; i > r; i--) {
      cnt[line[i]]++;
    }
    for (int i = r; i >= l; i--) {
      char c = line[i];
      res.emplace_back(c, i, cnt[c]++);
    }
  }
  return res;
}

template std::vector<std::tuple<char,int,int>>
VimMovementUtils::generateFMotions<true>(int,int,const std::string&,int);

template std::vector<std::tuple<char,int,int>>
VimMovementUtils::generateFMotions<false>(int,int,const std::string&,int);
