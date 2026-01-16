#include "VimCore/EdgeType.h"
#include "VimCore/LineEdgeType.h"
#include "VimCore/SentenceEdgeType.h"
#include "VimUtils.h"
#include "Utils/SentinelChar.h"
#include "VimMovementUtils.h"
#include "Editor/LineRange.h"

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

Position VimMovementUtils::motionWordEndpoint(Position cursor,
                                              const Lines& lines,
                                              bool forward,
                                              EdgeType edgeType,
                                              bool big,
                                              bool skipCurrent) {
  motionWord(cursor, lines, forward, edgeType, big, skipCurrent);
  return cursor;
}

// =============================================================================
// Text object range computation
// =============================================================================

Range VimMovementUtils::textObjectRange(
    Position cursor,
    const Lines& lines,
    bool isInner,
    bool isBigWord) {

  unsigned char c = lines.get(cursor);
  bool cursorOnWhitespace = isBlank(c);

  Position start, end;

  if (isInner) {
    // diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
    //
    // Special case: when cursor is on whitespace, text objects treat the
    // whitespace run as the selectable unit, not skipping to adjacent words.
    if (cursorOnWhitespace) {
      // Find start of whitespace run
      start = cursor;
      while (start.col > 0) {
        Position prev = lines.getPrevPosIncludeEmpty(start);
        if (prev == start) break;
        unsigned char pc = lines.get(prev);
        if (!isBlank(pc)) break;
        start = prev;
      }
      // Find end of whitespace run
      end = cursor;
      while (true) {
        Position next = lines.getNextPosIncludeEmpty(end);
        if (next == end) break;
        unsigned char nc = lines.get(next);
        if (!isBlank(nc)) break;
        end = next;
      }
    } else {
      // Cursor on word/symbol - use WordEdge motions
      start = motionWordEndpoint(cursor, lines, false, EdgeType::WordEdge, isBigWord, false);
      end = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);
    }
  } else {
    // daw/daW: depends on cursor position and trailing whitespace
    if (cursorOnWhitespace) {
      // Cursor in whitespace: (Backward, GapEdge) + (Forward, WordEdge)
      //
      // daw treats whitespace-under-cursor as "leading whitespace of next word".
      // If there's no next word, the operation is invalid.
      start = motionWordEndpoint(cursor, lines, false, EdgeType::GapEdge, isBigWord, false);
      end = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);

      // If forward motion ended on whitespace, no word was found - operation invalid
      if (isBlank(lines.get(end))) {
        return RANGE_NOT_FOUND;
      }
    } else {
      // Cursor in word/symbol: check for trailing whitespace/newline
      Position wordEnd = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);

      // Check char after word end for trailing whitespace/newline
      Position afterWord = lines.getNextPosIncludeEmpty(wordEnd);
      unsigned char afterChar = lines.get(afterWord);
      bool hasTrailingWs = isBlank(afterChar);  // includes newline

      if (hasTrailingWs) {
        // Has trailing ws/newline: (Backward, WordEdge) + (Forward, GapEdge)
        start = motionWordEndpoint(cursor, lines, false, EdgeType::WordEdge, isBigWord, false);
        end = motionWordEndpoint(cursor, lines, true, EdgeType::GapEdge, isBigWord, false);
      } else {
        // No trailing ws: (Backward, GapEdge) + (Forward, WordEdge)
        start = motionWordEndpoint(cursor, lines, false, EdgeType::GapEdge, isBigWord, false);
        end = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);
      }
    }
  }

  return Range(start, end);
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

// =============================================================================
// Paragraph motions - general interface (parallel to word motions)
// =============================================================================
//
// LineEdgeType is DIRECTION-INDEPENDENT:
//   BlockEdge: edge of current same-type block (blank or non-blank lines)
//   GapEdge:   edge of blank line run (adjacent to current paragraph)
//   NextEdge:  edge of next block (for }/{ motions - goes to blank line separator)
//
// Returns the line number where the motion lands.

int VimMovementUtils::motionParagraphEdge(int cursorLine,
                                          const Lines& lines,
                                          bool forward,
                                          LineEdgeType edgeType) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return 0;

  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isBlankLineStr(lines[cursorLine]);

  switch (edgeType) {
    case LineEdgeType::BlockEdge: {
      // Edge of current same-type block
      if (forward) {
        return paragraphEndLine(lines, cursorLine);
      } else {
        return paragraphStartLine(lines, cursorLine);
      }
    }

    case LineEdgeType::GapEdge: {
      // Edge of blank line run adjacent to current paragraph
      if (forward) {
        // Find end of trailing blank lines (after current paragraph)
        int blockEnd = paragraphEndLine(lines, cursorLine);
        if (cursorOnBlank) {
          // Already on blanks - return end of blank run
          return blockEnd;
        }
        // Skip past non-blank paragraph, find end of following blank run
        if (blockEnd + 1 < n && isBlankLineStr(lines[blockEnd + 1])) {
          return paragraphEndLine(lines, blockEnd + 1);
        }
        // No trailing blanks
        return blockEnd;
      } else {
        // Find start of leading blank lines (before current paragraph)
        int blockStart = paragraphStartLine(lines, cursorLine);
        if (cursorOnBlank) {
          // Already on blanks - return start of blank run
          return blockStart;
        }
        // Skip past non-blank paragraph, find start of preceding blank run
        if (blockStart > 0 && isBlankLineStr(lines[blockStart - 1])) {
          return paragraphStartLine(lines, blockStart - 1);
        }
        // No leading blanks
        return blockStart;
      }
    }

    case LineEdgeType::NextEdge: {
      // Edge of next block (}/{ motions go to blank line separator)
      if (forward) {
        // Skip current blank lines
        int i = cursorLine;
        while (i < n && isBlankLineStr(lines[i])) {
          i++;
        }
        if (i >= n) {
          // All blanks to end - return last line
          return n - 1;
        }
        // Scan forward for next blank line
        i++;
        while (i < n && !isBlankLineStr(lines[i])) {
          i++;
        }
        // Return blank line, or last line if not found
        return (i < n) ? i : n - 1;
      } else {
        // Skip current blank lines
        int i = cursorLine;
        while (i > 0 && isBlankLineStr(lines[i])) {
          i--;
        }
        // Scan backward for previous blank line
        i--;
        while (i >= 0 && !isBlankLineStr(lines[i])) {
          i--;
        }
        // Return blank line, or line 0 if not found
        return max(i, 0);
      }
    }
  }

  return cursorLine;  // Should not reach here
}

// =============================================================================
// Paragraph text object range computation (parallel to textObjectRange)
// =============================================================================
//
// From boundary-logic.md:
//   dip: (Backward, BlockEdge) + (Forward, BlockEdge)
//   dap: {
//     Cursor on non-blank line:
//       Has trailing blank lines: (Backward, BlockEdge) + (Forward, GapEdge)
//       Else: (Backward, GapEdge) + (Forward, BlockEdge)
//     Cursor on blank line:
//       (Backward, BlockEdge) + (Forward, NextEdge)
//   }

LineRange VimMovementUtils::paragraphTextObjectRange(int cursorLine,
                                                     const Lines& lines,
                                                     bool isInner) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return LINE_RANGE_NOT_FOUND;

  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isBlankLineStr(lines[cursorLine]);

  if (isInner) {
    // dip: (Backward, BlockEdge) + (Forward, BlockEdge)
    int startLine = motionParagraphEdge(cursorLine, lines, false, LineEdgeType::BlockEdge);
    int endLine = motionParagraphEdge(cursorLine, lines, true, LineEdgeType::BlockEdge);
    return LineRange(startLine, endLine);
  }

  // dap: depends on cursor position and trailing blank lines
  if (cursorOnBlank) {
    // Cursor on blank line: (Backward, BlockEdge) + (Forward, NextEdge)
    // Select blank run + following non-blank paragraph
    int startLine = motionParagraphEdge(cursorLine, lines, false, LineEdgeType::BlockEdge);
    int endLine = motionParagraphEdge(cursorLine, lines, true, LineEdgeType::NextEdge);

    // NextEdge forward finds the blank line after the next paragraph,
    // but we want to include the paragraph content, not stop at the blank.
    // Actually for "ap on blank", we want blank lines + following paragraph.
    // Let's use BlockEdge on the line after the blank run.
    int blankEnd = motionParagraphEdge(cursorLine, lines, true, LineEdgeType::BlockEdge);
    if (blankEnd + 1 < n) {
      // There's a non-blank paragraph after - include it
      endLine = motionParagraphEdge(blankEnd + 1, lines, true, LineEdgeType::BlockEdge);
    } else {
      // No paragraph after, just the blank lines
      endLine = blankEnd;
    }
    return LineRange(startLine, endLine);
  }

  // Cursor on non-blank line
  int blockEnd = motionParagraphEdge(cursorLine, lines, true, LineEdgeType::BlockEdge);

  // Check for trailing blank lines
  bool hasTrailingBlanks = (blockEnd + 1 < n && isBlankLineStr(lines[blockEnd + 1]));

  if (hasTrailingBlanks) {
    // Has trailing blank lines: (Backward, BlockEdge) + (Forward, GapEdge)
    int startLine = motionParagraphEdge(cursorLine, lines, false, LineEdgeType::BlockEdge);
    int endLine = motionParagraphEdge(cursorLine, lines, true, LineEdgeType::GapEdge);
    return LineRange(startLine, endLine);
  } else {
    // No trailing blanks: (Backward, GapEdge) + (Forward, BlockEdge)
    int startLine = motionParagraphEdge(cursorLine, lines, false, LineEdgeType::GapEdge);
    int endLine = motionParagraphEdge(cursorLine, lines, true, LineEdgeType::BlockEdge);
    return LineRange(startLine, endLine);
  }
}

void VimMovementUtils::motionParagraphPrev(Position &pos,
                                   const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  pos.line = motionParagraphEdge(pos.line, lines, false, LineEdgeType::NextEdge);
  pos.setCol(0);
}

void VimMovementUtils::motionParagraphNext(Position &pos,
                                   const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  int resultLine = motionParagraphEdge(pos.line, lines, true, LineEdgeType::NextEdge);
  pos.line = resultLine;

  // Special case: if at last line and it's not blank, go to last char
  // (This matches vim's behavior at EOF)
  if (resultLine == n - 1 && !isBlankLineStr(lines[resultLine])) {
    int lastCol = std::max(0, (int)lines[resultLine].size() - 1);
    pos.setCol(lastCol);
  } else {
    pos.setCol(0);
  }
}

// =============================================================================
// Sentence motions - general interface (parallel to word/paragraph motions)
// =============================================================================
//
// Sentence boundaries are defined by:
//   1. Punctuation [.!?] + optional closers [)'"'\]] + whitespace/EOL
//   2. Blank lines (paragraph boundary = sentence boundary)
//
// SentenceEdgeType:
//   SentenceEdge: edge of current sentence (at punctuation mark + closers)
//   GapEdge:      edge of whitespace gap after sentence end
//   NextEdge:     start of next sentence ()/( motions)

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

static std::pair<int, int>
skipToSentenceStart(const Lines &lines, int line, int col) {
  int n = (int)lines.size();
  if (n == 0) return {0, 0};

  int l = line, k = col;

  // Step 1: Move past the punctuation mark
  if (!stepFwd(lines, l, k)) {
    return {l, k};  // EOF after punctuation
  }

  // Step 2: Skip closers (can cross line boundaries)
  while (true) {
    unsigned char c = getChar(lines, l, k);
    if (!isSentenceCloser(c)) break;
    if (!stepFwd(lines, l, k)) return {l, k};  // EOF after closers
  }

  // Step 3: Skip whitespace; blank lines are sentence boundaries (stop there)
  while (true) {
    if (l >= n) return {n - 1, 0};

    // Blank line = sentence boundary, stop here
    if (isBlankLineStr(lines[l])) {
      return {l, 0};
    }

    int len = (int)lines[l].size();
    if (len == 0) {
      return {l, 0};  // Empty line = boundary
    }

    k = std::clamp(k, 0, len - 1);
    unsigned char c = (unsigned char)lines[l][k];

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

static std::pair<int, int>
findCurrentSentenceStart(const Lines &lines, int line, int col) {
  int n = (int)lines.size();
  if (n == 0) return {0, 0};

  line = std::clamp(line, 0, n - 1);
  if ((int)lines[line].size() == 0)
    col = 0;
  else
    col = std::clamp(col, 0, (int)lines[line].size() - 1);

  // If on blank line, we need to find the start of the LAST sentence
  // before the blank line (not the next sentence after).
  if (isBlankLineStr(lines[line])) {
    // Move to last non-blank line before this blank run
    int prevLine = line - 1;
    while (prevLine >= 0 && isBlankLineStr(lines[prevLine])) {
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
      unsigned char c = getChar(lines, l, k);
      if (c == '.' || c == '!' || c == '?' || isSentenceCloser(c)) {
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
      if (isBlankLineStr(lines[l])) {
        // Sentence starts at first non-blank after this blank run
        int nextLine = l + 1;
        while (nextLine < n && isBlankLineStr(lines[nextLine])) ++nextLine;
        if (nextLine >= n) return {n - 1, 0};
        return {nextLine, firstNonBlankColInLineStr(lines[nextLine])};
      }

      // Step backward
      int pl = l, pk = k;
      if (!stepBack(lines, pl, pk)) {
        // Reached buffer start - sentence starts at first non-blank
        int i = 0;
        while (i < n && isBlankLineStr(lines[i])) ++i;
        if (i >= n) return {0, 0};
        return {i, firstNonBlankColInLineStr(lines[i])};
      }

      // Check if stepping back crossed into a blank line
      if (isBlankLineStr(lines[pl])) {
        // Sentence starts at line l
        return {l, firstNonBlankColInLineStr(lines[l])};
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
    unsigned char c = getChar(lines, l, k);
    while (c == '.' || c == '!' || c == '?' || isSentenceCloser(c)) {
      if (!stepBack(lines, l, k)) break;
      c = getChar(lines, l, k);
    }
  }

  // If we're in whitespace, check if it follows a sentence end.
  // If so, we're "between" sentences and should return the start of
  // the sentence that ENDED (not the next one after the whitespace).
  {
    unsigned char c = getChar(lines, l, k);
    if (c == ' ' || c == '\t') {
      // Search backward through whitespace to find potential sentence end
      int tl = l, tk = k;
      while (tk > 0) {
        --tk;
        unsigned char tc = getChar(lines, tl, tk);
        if (tc == '.' || tc == '!' || tc == '?') {
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
                while (i < n && isBlankLineStr(lines[i])) ++i;
                if (i >= n) return {0, 0};
                return {i, firstNonBlankColInLineStr(lines[i])};
              }
            }
            break;
          }
        }
        if (tc != ' ' && tc != '\t' && !isSentenceCloser(tc)) {
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
    if (isBlankLineStr(lines[l])) {
      // Sentence starts at first non-blank after this blank run
      int nextLine = l + 1;
      while (nextLine < n && isBlankLineStr(lines[nextLine])) ++nextLine;
      if (nextLine >= n) return {n - 1, 0};
      return {nextLine, firstNonBlankColInLineStr(lines[nextLine])};
    }

    // Step backward
    int pl = l, pk = k;
    if (!stepBack(lines, pl, pk)) {
      // Reached buffer start - sentence starts at first non-blank
      int i = 0;
      while (i < n && isBlankLineStr(lines[i])) ++i;
      if (i >= n) return {0, 0};
      return {i, firstNonBlankColInLineStr(lines[i])};
    }

    // Check if stepping back crossed into a blank line
    if (isBlankLineStr(lines[pl])) {
      // Sentence starts at line l
      return {l, firstNonBlankColInLineStr(lines[l])};
    }

    l = pl;
    k = pk;
  }
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

// =============================================================================
// motionSentenceEdge - Unified sentence motion implementation
// =============================================================================
//
// Handles all sentence edge types in both directions.
// This parallels motionWord() and motionParagraphEdge().

Position VimMovementUtils::motionSentenceEdge(Position cursor,
                                               const Lines& lines,
                                               bool forward,
                                               SentenceEdgeType edgeType) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return cursor;

  int line = std::clamp(cursor.line, 0, n - 1);
  int col = lines[line].empty() ? 0 : std::clamp(cursor.col, 0, static_cast<int>(lines[line].size()) - 1);

  if (forward) {
    // Forward motion - find next sentence boundary

    // If on blank line, skip to first non-blank line
    if (isBlankLineStr(lines[line])) {
      while (line < n && isBlankLineStr(lines[line])) {
        line++;
      }
      if (line >= n) return Position(n - 1, 0);

      // For NextEdge: return start of first non-blank line (sentence start)
      if (edgeType == SentenceEdgeType::NextEdge) {
        return Position(line, firstNonBlankColInLineStr(lines[line]));
      }
      // For SentenceEdge/GapEdge: continue searching from this line
      col = 0;
    }

    // Search forward for sentence end
    int l = line, k = col;
    while (true) {
      if (isSentenceEndAt(lines, l, k)) {
        // Found sentence end at (l, k)
        int endLine = l, endCol = k;

        // Skip past the punctuation mark
        if (!stepFwd(lines, l, k)) {
          // At EOF - return sentence end position
          if (edgeType == SentenceEdgeType::SentenceEdge) {
            // Return position of last closer (or punctuation if no closers)
            return Position(endLine, endCol);
          }
          return Position(endLine, endCol);
        }

        // Skip closers on same line
        while (true) {
          unsigned char c = getChar(lines, l, k);
          if (!isSentenceCloser(c)) break;
          endLine = l;
          endCol = k;
          int tl = l, tk = k;
          if (!stepFwd(lines, tl, tk)) break;
          if (tl != l) break;
          l = tl;
          k = tk;
        }

        // For SentenceEdge: return position of last closer (or punctuation)
        if (edgeType == SentenceEdgeType::SentenceEdge) {
          return Position(endLine, endCol);
        }

        // Now skip whitespace and blank lines to find gap edge or next sentence
        int gapEndLine = l, gapEndCol = k;
        bool foundNonBlank = false;

        while (true) {
          if (l >= n) break;
          if (isBlankLineStr(lines[l])) {
            gapEndLine = l;
            gapEndCol = 0;
            l++;
            k = 0;
            continue;
          }
          int len = static_cast<int>(lines[l].size());
          if (len == 0) {
            l++;
            k = 0;
            continue;
          }
          k = std::clamp(k, 0, len - 1);
          unsigned char c = static_cast<unsigned char>(lines[l][k]);
          if (c == ' ' || c == '\t') {
            gapEndLine = l;
            gapEndCol = k;
            if (!stepFwd(lines, l, k)) break;
            continue;
          }
          // Found non-blank
          foundNonBlank = true;
          break;
        }

        if (edgeType == SentenceEdgeType::GapEdge) {
          return Position(gapEndLine, gapEndCol);
        }

        // NextEdge: return start of next sentence
        if (l >= n) return Position(n - 1, 0);
        return Position(l, k);
      }

      if (!stepFwd(lines, l, k)) {
        // Reached end of buffer without finding sentence end
        return cursor;
      }
    }
  } else {
    // Backward motion - find previous sentence boundary

    // First find start of current sentence
    auto [sl, sc] = findCurrentSentenceStart(lines, line, col);

    // If we're already at a sentence start, we need to go to the previous one
    if (sl == line && sc == col) {
      int l = sl, k = sc;
      if (stepBack(lines, l, k)) {
        auto [psl, psc] = findCurrentSentenceStart(lines, l, k);
        sl = psl;
        sc = psc;
      } else {
        // At buffer start, can't go back
        return Position(sl, sc);
      }
    }

    // For NextEdge (( motion): return sentence start
    if (edgeType == SentenceEdgeType::NextEdge) {
      return Position(sl, sc);
    }

    // For SentenceEdge: find the sentence end before this sentence start
    // Go back from sentence start to find the previous sentence's end
    if (sl == 0 && sc == 0) {
      // At buffer start
      return Position(0, 0);
    }

    int l = sl, k = sc;
    if (!stepBack(lines, l, k)) {
      return Position(0, 0);
    }

    // Skip whitespace/blank lines backward to find gap start or sentence end
    while (true) {
      if (isBlankLineStr(lines[l])) {
        if (edgeType == SentenceEdgeType::GapEdge) {
          // Find start of blank run
          while (l > 0 && isBlankLineStr(lines[l - 1])) {
            l--;
          }
          return Position(l, 0);
        }
        // Skip blank lines
        while (l > 0 && isBlankLineStr(lines[l])) {
          l--;
        }
        if (isBlankLineStr(lines[l])) {
          // All blank lines to start
          return Position(0, 0);
        }
        k = static_cast<int>(lines[l].size()) - 1;
        if (k < 0) k = 0;
        continue;
      }

      unsigned char c = getChar(lines, l, k);
      if (c == ' ' || c == '\t') {
        if (edgeType == SentenceEdgeType::GapEdge) {
          // Skip forward past whitespace to find gap end
          while (true) {
            unsigned char nc = getChar(lines, l, k);
            if (nc != ' ' && nc != '\t') break;
            int tl = l, tk = k;
            if (!stepFwd(lines, tl, tk)) break;
            if (tl != l) break;
            l = tl;
            k = tk;
          }
          // Step back to last whitespace
          if (!stepBack(lines, l, k)) return Position(0, 0);
          return Position(l, k);
        }
        // Skip whitespace backward
        if (!stepBack(lines, l, k)) {
          return Position(0, 0);
        }
        continue;
      }

      // Found non-whitespace - this should be sentence end (closer or punctuation)
      // Skip closers backward
      while (isSentenceCloser(c)) {
        if (!stepBack(lines, l, k)) {
          return Position(l, k);
        }
        c = getChar(lines, l, k);
      }

      // Should be at punctuation mark
      if (c == '.' || c == '!' || c == '?') {
        return Position(l, k);
      }

      // If not at sentence-ending punctuation, continue backward
      if (!stepBack(lines, l, k)) {
        return Position(0, 0);
      }
    }
  }
}

// =============================================================================
// sentenceTextObjectRange - Sentence text object range computation
// =============================================================================
//
// Parallels textObjectRange() for words and paragraphTextObjectRange() for paragraphs.
//
// From boundary-logic.md pattern:
//   dis: (Backward, SentenceEdge) + (Forward, SentenceEdge)
//   das: {
//     Has trailing whitespace: (Backward, SentenceEdge) + (Forward, GapEdge)
//     Else: (Backward, GapEdge) + (Forward, SentenceEdge)
//   }

Range VimMovementUtils::sentenceTextObjectRange(Position cursor,
                                                 const Lines& lines,
                                                 bool isInner) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return RANGE_NOT_FOUND;

  // Find sentence start (beginning of current sentence)
  auto [startLine, startCol] = findCurrentSentenceStart(lines, cursor.line, cursor.col);

  // Find sentence end by searching forward from sentence start
  Position sentenceStart(startLine, startCol);
  Position sentenceEnd = motionSentenceEdge(sentenceStart, lines, true, SentenceEdgeType::SentenceEdge);

  if (isInner) {
    // dis: just the sentence content
    return Range(sentenceStart, sentenceEnd);
  }

  // das: include trailing whitespace (or leading if no trailing)
  Position gapEnd = motionSentenceEdge(sentenceStart, lines, true, SentenceEdgeType::GapEdge);

  // Check if there's trailing whitespace/blank lines
  bool hasTrailing = (gapEnd.line > sentenceEnd.line ||
                      (gapEnd.line == sentenceEnd.line && gapEnd.col > sentenceEnd.col));

  if (hasTrailing) {
    // Include trailing whitespace
    return Range(sentenceStart, gapEnd);
  }

  // No trailing whitespace - include leading whitespace
  // Find gap edge backward from sentence start
  Position gapStart = motionSentenceEdge(sentenceStart, lines, false, SentenceEdgeType::GapEdge);

  // Check if there's leading whitespace
  bool hasLeading = (gapStart.line < sentenceStart.line ||
                     (gapStart.line == sentenceStart.line && gapStart.col < sentenceStart.col));

  if (hasLeading) {
    return Range(gapStart, sentenceEnd);
  }

  // No surrounding whitespace, just return sentence
  return Range(sentenceStart, sentenceEnd);
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
  // Special case: if all remaining lines are blank, move to next blank line (if any).
  if (isBlankLineStr(lines[line])) {
    int startLine = line;
    while (line < n && isBlankLineStr(lines[line]))
      ++line;
    if (line >= n) {
      // All remaining lines are blank - move to next blank line if we can
      if (startLine + 1 < n) {
        pos.line = startLine + 1;
        pos.setCol(0);
      }
      return;
    }
    pos.line = line;
    pos.setCol(firstNonBlankColInLineStr(lines[line]));
    return;
  }

  // Check if we're on whitespace/closer that follows a sentence end.
  // If so, skip directly to next sentence start (we're in the gap).
  {
    unsigned char c = getChar(lines, line, col);
    if (c == ' ' || c == '\t' || isSentenceCloser(c)) {
      // Search backward to see if there's a sentence end before us on this line
      int l = line, k = col;
      while (k > 0) {
        --k;
        unsigned char pc = getChar(lines, l, k);
        if (pc == '.' || pc == '!' || pc == '?') {
          // Found punctuation - check if it's a valid sentence end
          if (isSentenceEndAt(lines, l, k)) {
            // We're in the gap after a sentence end - skip to next sentence start
            auto [nl, nk] = skipToSentenceStart(lines, l, k);
            pos.line = nl;
            pos.setCol(nk);
            return;
          }
        }
        if (pc != ' ' && pc != '\t' && !isSentenceCloser(pc)) {
          // Hit non-whitespace/non-closer that's not punctuation
          break;
        }
      }
    }
  }

  // Search forward for sentence end, then skip to next sentence start
  int l = line, k = col;
  while (true) {
    if (isSentenceEndAt(lines, l, k)) {
      auto [nl, nk] = skipToSentenceStart(lines, l, k);
      pos.line = nl;
      pos.setCol(nk);
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

  auto [sl, sc] = findCurrentSentenceStart(lines, pos.line, pos.col);

  // If already at sentence start, go to previous sentence start.
  if (sl == pos.line && sc == pos.col) {
    int l = sl, k = sc;

    // Step back past the whitespace/closers that precede this sentence start
    // to get into the content of the previous sentence
    while (true) {
      if (!stepBack(lines, l, k)) {
        // At buffer start, can't go further
        pos.line = sl;
        pos.setCol(sc);
        return;
      }

      // Blank line IS a sentence boundary - stop here
      if (isBlankLineStr(lines[l])) {
        pos.line = l;
        pos.setCol(0);
        return;
      }

      unsigned char c = getChar(lines, l, k);
      // Skip whitespace, closers, and sentence-ending punctuation
      if (c == ' ' || c == '\t' || isSentenceCloser(c) ||
          c == '.' || c == '!' || c == '?') {
        continue;
      }

      // We're now in actual content - find this sentence's start
      break;
    }

    auto [psl, psc] = findCurrentSentenceStart(lines, l, k);
    pos.line = psl;
    pos.setCol(psc);
    return;
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
