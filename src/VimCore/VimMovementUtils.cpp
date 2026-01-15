#include "Boundary/EditBoundary.h"
#include "VimCore/EndpointType.h"
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
// Unified word motion based on (direction, endpointType, isWORD).
//
// EndpointType determines WHERE we stop:
//   End:   word END position (current is word char, next is not)
//   Space: position just BEFORE next word start (the trailing space/char)
//   Next:  word START position (current is word char, prev is not)
//
// For MOTIONS (cursor movement):
//   e  = Forward + End    (land on word end)
//   w  = Forward + Next   (land on word start)
//   b  = Backward + Next  (land on word start)
//   ge = Backward + End   (land on word end)
//
// For DELETIONS (different endpoints for some):
//   de  = Forward + End
//   dw  = Forward + Space  (delete including trailing whitespace)
//   db  = Backward + End   (delete to previous word end, not start)
//   dge = Backward + Next
//
// =============================================================================

namespace {

// =============================================================================
// Forward WORD motion (big = true)
// =============================================================================
// Two char types: space vs non-space
//
// Algorithm:
//   if starting on space:
//     skip to first non-space
//     Next? return pos (first non-space)
//     Space? return pos-1 (last space)
//     End? continue below...
//
//   skip non-spaces (current WORD)
//   End? return pos-1 (last char of WORD)
//
//   skip spaces
//   Space? return pos-1 (last space)
//   Next? return pos (first non-space)
//
void motionWordForward_big(Position &pos, const Lines &lines,
                           EndpointType endpoint) {
    int line = pos.line, col = pos.col;
    unsigned char c = getChar(lines, line, col);
    if (c == 0) return;

    int prevLine, prevCol;

    // === Starting on space: skip to first non-space ===
    if (isBlank(c)) {
        do {
            prevLine = line; prevCol = col;
            if (!stepFwd(lines, line, col)) {
                pos.line = line; pos.setCol(static_cast<int>(lines[line].size()));
                return;
            }
            c = getChar(lines, line, col);
        } while (isBlank(c));

        if (endpoint == EndpointType::Next) {
            pos.line = line; pos.setCol(col);
            return;
        }
        if (endpoint == EndpointType::Space) {
            pos.line = prevLine; pos.setCol(prevCol);
            return;
        }
        // End: continue to find WORD end below
    }

    // === Skip non-spaces (current WORD) ===
    do {
        prevLine = line; prevCol = col;
        if (!stepFwd(lines, line, col)) {
            // EOF
            if (endpoint == EndpointType::End) {
                pos.line = prevLine; pos.setCol(prevCol);
            } else {
                pos.line = line; pos.setCol(static_cast<int>(lines[line].size()));
            }
            return;
        }
        c = getChar(lines, line, col);
    } while (!isBlank(c));

    if (endpoint == EndpointType::End) {
        pos.line = prevLine; pos.setCol(prevCol);
        return;
    }

    // === Skip spaces ===
    do {
        prevLine = line; prevCol = col;
        if (!stepFwd(lines, line, col)) {
            pos.line = line; pos.setCol(static_cast<int>(lines[line].size()));
            return;
        }
        c = getChar(lines, line, col);
    } while (isBlank(c));

    if (endpoint == EndpointType::Space) {
        pos.line = prevLine; pos.setCol(prevCol);
    } else {  // Next
        pos.line = line; pos.setCol(col);
    }
}

// =============================================================================
// Forward word motion (big = false)
// =============================================================================
// Three char types: space, wordchar [a-zA-Z0-9_], symbol (other non-space)
//
// Algorithm:
//   if starting on space:
//     skip to first non-space
//     Next? return pos (first non-space)
//     Space? return pos-1 (last space)
//     End? continue below...
//
//   note type (wordchar vs symbol), skip same-type chars
//   End? return pos-1 (last char of word)
//
//   if now on non-space (different type = new word start):
//     Next? return pos
//     Space? return pos-1
//
//   skip spaces
//   Space? return pos-1 (last space)
//   Next? return pos (first non-space)
//
void motionWordForward_small(Position &pos, const Lines &lines,
                             EndpointType endpoint) {
    int line = pos.line, col = pos.col;
    unsigned char c = getChar(lines, line, col);
    if (c == 0) return;

    int prevLine, prevCol;

    // === Starting on space: skip to first non-space ===
    if (isBlank(c)) {
        do {
            prevLine = line; prevCol = col;
            if (!stepFwd(lines, line, col)) {
                pos.line = line; pos.setCol(static_cast<int>(lines[line].size()));
                return;
            }
            c = getChar(lines, line, col);
        } while (isBlank(c));

        if (endpoint == EndpointType::Next) {
            pos.line = line; pos.setCol(col);
            return;
        }
        if (endpoint == EndpointType::Space) {
            pos.line = prevLine; pos.setCol(prevCol);
            return;
        }
        // End: continue to find word end below
    }

    // === Skip same-type chars (current word) ===
    bool startIsWordChar = isSmallWordChar(c);
    do {
        prevLine = line; prevCol = col;
        if (!stepFwd(lines, line, col)) {
            // EOF
            if (endpoint == EndpointType::End) {
                pos.line = prevLine; pos.setCol(prevCol);
            } else {
                pos.line = line; pos.setCol(static_cast<int>(lines[line].size()));
            }
            return;
        }
        c = getChar(lines, line, col);
    } while (!isBlank(c) && isSmallWordChar(c) == startIsWordChar);

    if (endpoint == EndpointType::End) {
        pos.line = prevLine; pos.setCol(prevCol);
        return;
    }

    // === If at non-space (different type = new word start) ===
    if (!isBlank(c)) {
        if (endpoint == EndpointType::Next) {
            pos.line = line; pos.setCol(col);
        } else {  // Space
            pos.line = prevLine; pos.setCol(prevCol);
        }
        return;
    }

    // === Skip spaces ===
    do {
        prevLine = line; prevCol = col;
        if (!stepFwd(lines, line, col)) {
            pos.line = line; pos.setCol(static_cast<int>(lines[line].size()));
            return;
        }
        c = getChar(lines, line, col);
    } while (isBlank(c));

    if (endpoint == EndpointType::Space) {
        pos.line = prevLine; pos.setCol(prevCol);
    } else {  // Next
        pos.line = line; pos.setCol(col);
    }
}

// =============================================================================
// Backward WORD motion (big = true)
// =============================================================================
// Algorithm (mirrored from forward):
//   if starting on space:
//     skip to first non-space (backward)
//     End? return pos (last non-space = WORD end)
//     Space? return pos (same position)
//     Next? continue below...
//
//   skip non-spaces backward (current WORD)
//   Next? return pos+1 (first char of WORD = WORD start)
//
//   skip spaces backward
//   Space? return pos+1 (first space)
//   End? return pos (last non-space = WORD end)
//
void motionWordBackward_big(Position &pos, const Lines &lines,
                            EndpointType endpoint) {
    int line = pos.line, col = pos.col;
    unsigned char c = getChar(lines, line, col);
    if (c == 0) return;

    int nextLine, nextCol;

    // === Starting on space: skip to first non-space ===
    if (isBlank(c)) {
        do {
            nextLine = line; nextCol = col;
            if (!stepBack(lines, line, col)) return;
            c = getChar(lines, line, col);
        } while (isBlank(c));

        if (endpoint == EndpointType::End) {
            pos.line = line; pos.setCol(col);
            return;
        }
        if (endpoint == EndpointType::Space) {
            pos.line = line; pos.setCol(col);
            return;
        }
        // Next: continue to find WORD start below
    }

    // === Skip non-spaces backward (current WORD) ===
    do {
        nextLine = line; nextCol = col;
        if (!stepBack(lines, line, col)) {
            // BOF - at WORD start
            if (endpoint == EndpointType::Next) {
                pos.line = nextLine; pos.setCol(nextCol);
            }
            return;
        }
        c = getChar(lines, line, col);
    } while (!isBlank(c));

    if (endpoint == EndpointType::Next) {
        pos.line = nextLine; pos.setCol(nextCol);
        return;
    }
    if (endpoint == EndpointType::Space) {
        pos.line = line; pos.setCol(col);
        return;
    }

    // === Skip spaces backward for End ===
    do {
        if (!stepBack(lines, line, col)) return;
        c = getChar(lines, line, col);
    } while (isBlank(c));

    pos.line = line; pos.setCol(col);
}

// =============================================================================
// Backward word motion (big = false)
// =============================================================================
// Algorithm (mirrored from forward small):
//   if starting on space:
//     skip to first non-space (backward)
//     End? return pos (word end)
//     Space? return pos (same position)
//     Next? continue below...
//
//   note type (wordchar vs symbol), skip same-type chars backward
//   Next? return pos+1 (first char of word = word start)
//
//   if now on non-space (different type = word end of adjacent word):
//     Space? return pos
//     End? return pos
//
//   skip spaces backward
//   Space? return pos
//   End? return pos (word end)
//
void motionWordBackward_small(Position &pos, const Lines &lines,
                              EndpointType endpoint) {
    int line = pos.line, col = pos.col;
    unsigned char c = getChar(lines, line, col);
    if (c == 0) return;

    int nextLine, nextCol;

    // === Starting on space: skip to first non-space ===
    if (isBlank(c)) {
        do {
            nextLine = line; nextCol = col;
            if (!stepBack(lines, line, col)) return;
            c = getChar(lines, line, col);
        } while (isBlank(c));

        if (endpoint == EndpointType::End) {
            pos.line = line; pos.setCol(col);
            return;
        }
        if (endpoint == EndpointType::Space) {
            pos.line = line; pos.setCol(col);
            return;
        }
        // Next: continue to find word start below
    }

    // === Skip same-type chars backward (current word) ===
    bool startIsWordChar = isSmallWordChar(c);
    do {
        nextLine = line; nextCol = col;
        if (!stepBack(lines, line, col)) {
            // BOF - at word start
            if (endpoint == EndpointType::Next) {
                pos.line = nextLine; pos.setCol(nextCol);
            }
            return;
        }
        c = getChar(lines, line, col);
    } while (!isBlank(c) && isSmallWordChar(c) == startIsWordChar);

    if (endpoint == EndpointType::Next) {
        pos.line = nextLine; pos.setCol(nextCol);
        return;
    }

    // === If at non-space (different type = word end of adjacent word) ===
    if (!isBlank(c)) {
        // Both Space and End return current position
        pos.line = line; pos.setCol(col);
        return;
    }

    // === At space ===
    if (endpoint == EndpointType::Space) {
        pos.line = line; pos.setCol(col);
        return;
    }

    // === Skip spaces backward for End ===
    do {
        if (!stepBack(lines, line, col)) return;
        c = getChar(lines, line, col);
    } while (isBlank(c));

    pos.line = line; pos.setCol(col);
}


} // anonymous namespace


void motionWORDImpl(Position& pos, const Lines& lines, bool forward, EndpointType endpointType) {
  if(isBlank(lines.get(pos))) {
    // handle separately
  }

  Position lastPos = lines.getLastPos();
  while(isBigWordChar(lines.get(pos))) {
    pos = lines.getNextPos(pos);
    if(pos == lastPos) return;
  }
  // Now at first non-WORD
  assert(isBlank(lines.get(pos)));
  if(endpointType == EndpointType::End) {
    pos = lines.getPrevPos(pos);
    return;
  }
  while(!isBlank (lines.get(pos))) {
    pos = lines.getNextPos(pos);
    if(pos == lastPos) return;
  }
  // Now at start of next WORD 
  assert(isBigWordChar(lines.get(pos)));
  if(endpointType == EndpointType::Space) {
    pos = lines.getPrevPos(pos);
    return;
  } else if(endpointType == EndpointType::Next) {
    return;
  } else {
    assert(false && "not implemented yet");
  }
}


void motionWordImpl(Position& pos, const Lines& lines, bool forward, EndpointType endpointType) {
  if(isBlank(lines.get(pos))) {
    // handle separately
  }
  Position lastPos = lines.getLastPos();

  char c = lines.get(pos);
  CharType currentWordType = getCharType(c);
  CharType oppositeWordType = getOppositeCharType(currentWordType);

  while(getCharType(lines.get(pos)) == currentWordType) {
    pos = lines.getNextPos(pos);
    if(pos == lastPos) return;
  }
  // Now at first non-currentWordType
  assert(getCharType(lines.get(pos)) != currentWordType);
  if(endpointType == EndpointType::End) {
    pos = lines.getPrevPos(pos);
    return;
  }
  while(isBlank(lines.get(pos))) {
    pos = lines.getNextPos(pos);
    if(pos == lastPos) return;
  }
  // Now at start of next word
  assert(!isBlank(lines.get(pos)));
  if(endpointType == EndpointType::Space) {
    pos = lines.getPrevPos(pos);
    return;
  } else if(endpointType == EndpointType::Next) {
    return;
  } else {
    assert(false && "not implemented yet");
  }
}

void VimMovementUtils::motionWord(Position &pos,
                                   const Lines &lines,
                                   bool forward,
                                   EndpointType endpointType,
                                   bool big,
                                   bool skipCurrent
                                   ) {
  if(skipCurrent) {
    if(forward) {
      pos = lines.getNextPos(pos);
    } else {
      pos = lines.getPrevPos(pos);
    }
  }
  if(big) {
    motionWORDImpl(pos, lines, forward, endpointType);
  } else {
    motionWordImpl(pos, lines, forward, endpointType);
  }
}

// Returns true if doing motion from pos would get to lastPos
bool VimMovementUtils::checkMotionWordReaches(Position pos,
                                     const Position& lastPos,
                                     const Lines& lines, 
                                     bool forward,
                                     EndpointType endpointType,
                                     bool big,
                                     bool skipCurrent
                                     ) {
  // To same thing as motionWord, but return true/false (can return true early) if the motion would bring up to lastPos
}



// =============================================================================
// Named forwarders - handle +1 shift where needed
// =============================================================================
//
// From edit-boundary-logic.md:
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
