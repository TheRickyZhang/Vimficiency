#include "VimEndpointUtils.h"
#include "VimCore.h"

#include <algorithm>
#include <cassert>

#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"

using namespace std;

namespace VimCore {

// =============================================================================
// Word endpoint/range computation
// =============================================================================

Position motionWordEndpoint(Position cursor,
                            const Lines& lines,
                            bool forward,
                            EdgeType edgeType,
                            bool big,
                            bool skipCurrent,
                            int boundaryOffset,
                            bool hasLinesOutside) {
  // Use raw motion which returns POSITION_OUTSIDE_BOUNDARY if it would go past buffer
  Position result = motionWordCore(cursor, lines, forward, edgeType, big, skipCurrent);

  // If motion hit buffer boundary, check what kind of crossing this is
  if (result == POSITION_OUTSIDE_BOUNDARY) {
    if (hasLinesOutside) {
      // There are lines beyond the buffer edge - line-level crossing
      return POSITION_OUTSIDE_BOUNDARY;
    }
    if (boundaryOffset > 0) {
      // Motion went to buffer edge, which means it went through the protected
      // column region (forward: suffix at line end, backward: prefix at line start)
      return POSITION_OUTSIDE_BOUNDARY;
    }
    // No lines outside, no column protection - motion just hit buffer edge, clamp
    return cursor;
  }

  int lastLine = lines.lastLine();

  // Check if result is in protected column boundary region
  if (boundaryOffset > 0) {
    if (forward) {
      // Forward: protect suffix (last boundaryOffset cols of last line)
      if (result.line == lastLine) {
        int lineLen = static_cast<int>(lines[lastLine].size());
        if (result.col >= lineLen - boundaryOffset) {
          return POSITION_OUTSIDE_BOUNDARY;
        }
      }
    } else {
      // Backward: protect prefix (first boundaryOffset cols of line 0)
      if (result.line == 0 && result.col < boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }

  return result;
}

// Helper to compute whitespace run range (used for iw/iW when cursor on whitespace)
static Range computeWhitespaceRun(Position cursor, const Lines& lines) {
  Position start = cursor;
  while (start.col > 0) {
    Position prev = lines.getPrevPos(start);
    if (prev == start) break;
    unsigned char pc = lines.get(prev);
    if (!isBlank(pc)) break;
    start = prev;
  }
  Position end = cursor;
  while (true) {
    Position next = lines.getNextPos(end);
    if (next == end) break;
    unsigned char nc = lines.get(next);
    if (!isBlank(nc)) break;
    end = next;
  }
  return Range(start, end);
}

Range textObjectCore(
    Position cursor,
    const Lines& lines,
    bool isInner,
    bool isBigWord) {

  unsigned char c = lines.get(cursor);
  bool cursorOnWhitespace = isBlank(c);

  Position start, end;

  if (isInner) {
    // diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
    if (cursorOnWhitespace) {
      // Whitespace run - this doesn't use motionWordCore, always valid
      return computeWhitespaceRun(cursor, lines);
    } else {
      // Cursor on word/symbol - use motionWordCore directly
      start = motionWordCore(cursor, lines, false, EdgeType::WordEdge, isBigWord, false);
      end = motionWordCore(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);
    }
  } else {
    // daw/daW: depends on cursor position and trailing whitespace
    if (cursorOnWhitespace) {
      // Cursor in whitespace: (Backward, GapEdge) + (Forward, WordEdge)
      start = motionWordCore(cursor, lines, false, EdgeType::GapEdge, isBigWord, false);
      end = motionWordCore(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);
    } else {
      // Cursor in word/symbol: check for trailing whitespace/newline
      // Use motionWordCore to find word end, check if POSITION_OUTSIDE_BOUNDARY
      Position wordEnd = motionWordCore(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);
      bool hasTrailingWs = false;
      if (wordEnd != POSITION_OUTSIDE_BOUNDARY) {
        Position afterWord = lines.getNextPos(wordEnd);
        unsigned char afterChar = lines.get(afterWord);
        hasTrailingWs = isBlank(afterChar);
      }

      if (hasTrailingWs) {
        // Has trailing ws/newline: (Backward, WordEdge) + (Forward, GapEdge)
        start = motionWordCore(cursor, lines, false, EdgeType::WordEdge, isBigWord, false);
        end = motionWordCore(cursor, lines, true, EdgeType::GapEdge, isBigWord, false);
      } else {
        // No trailing ws: (Backward, GapEdge) + (Forward, WordEdge)
        start = motionWordCore(cursor, lines, false, EdgeType::GapEdge, isBigWord, false);
        end = motionWordCore(cursor, lines, true, EdgeType::WordEdge, isBigWord, false);
      }
    }
  }

  // Return Range where start/end could be POSITION_OUTSIDE_BOUNDARY
  return Range(start, end);
}

Range textObject(
    Position cursor,
    const Lines& lines,
    bool isInner,
    bool isBigWord) {

  Range range = textObjectCore(cursor, lines, isInner, isBigWord);

  // Clamp POSITION_OUTSIDE_BOUNDARY to buffer edges
  if (range.start == POSITION_OUTSIDE_BOUNDARY) {
    range.start = Position(0, 0);
  }
  if (range.end == POSITION_OUTSIDE_BOUNDARY) {
    int lastLine = lines.lastLine();
    int lastCol = lines[lastLine].empty() ? 0 : static_cast<int>(lines[lastLine].size()) - 1;
    range.end = Position(lastLine, lastCol);
  }

  return range;
}

Range textObjectRange(
    Position cursor,
    const Lines& lines,
    bool isInner,
    bool isBigWord,
    int leftColOffset,
    int rightColOffset,
    bool hasLinesAbove,
    bool hasLinesBelow) {

  unsigned char c = lines.get(cursor);
  bool cursorOnWhitespace = isBlank(c);

  Position start, end;

  if (isInner) {
    // diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
    if (cursorOnWhitespace) {
      // Whitespace run doesn't use motionWordEndpoint, so no line crossing possible
      // But we still need to check column boundaries on the result
      Range wsRange = computeWhitespaceRun(cursor, lines);
      start = wsRange.start;
      end = wsRange.end;

      // Check left boundary
      if (leftColOffset > 0 && start.line == 0 && start.col < leftColOffset) {
        start = POSITION_OUTSIDE_BOUNDARY;
      }
      // Check right boundary
      if (rightColOffset > 0) {
        int lastLine = lines.lastLine();
        if (end.line == lastLine) {
          int lineLen = static_cast<int>(lines[lastLine].size());
          if (end.col >= lineLen - rightColOffset) {
            end = POSITION_OUTSIDE_BOUNDARY;
          }
        }
      }
    } else {
      // Cursor on word/symbol - use WordEdge motions WITH boundary checking
      start = motionWordEndpoint(cursor, lines, false, EdgeType::WordEdge, isBigWord, false, leftColOffset, hasLinesAbove);
      end = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false, rightColOffset, hasLinesBelow);
    }
  } else {
    // daw/daW: depends on cursor position and trailing whitespace
    if (cursorOnWhitespace) {
      // Cursor in whitespace: (Backward, GapEdge) + (Forward, WordEdge)
      start = motionWordEndpoint(cursor, lines, false, EdgeType::GapEdge, isBigWord, false, leftColOffset, hasLinesAbove);
      end = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false, rightColOffset, hasLinesBelow);

      // If forward motion ended on whitespace, no word was found - signal invalid
      if (end != POSITION_OUTSIDE_BOUNDARY && isBlank(lines.get(end))) {
        end = POSITION_OUTSIDE_BOUNDARY;
      }
    } else {
      // Cursor in word/symbol: check for trailing whitespace/newline
      // First check wordEnd without boundary to determine trailing ws
      Position wordEnd = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false, 0, false);
      bool hasTrailingWs = false;
      if (wordEnd != POSITION_OUTSIDE_BOUNDARY) {
        Position afterWord = lines.getNextPos(wordEnd);
        unsigned char afterChar = lines.get(afterWord);
        hasTrailingWs = isBlank(afterChar);
      }

      if (hasTrailingWs) {
        // Has trailing ws/newline: (Backward, WordEdge) + (Forward, GapEdge)
        start = motionWordEndpoint(cursor, lines, false, EdgeType::WordEdge, isBigWord, false, leftColOffset, hasLinesAbove);
        end = motionWordEndpoint(cursor, lines, true, EdgeType::GapEdge, isBigWord, false, rightColOffset, hasLinesBelow);
      } else {
        // No trailing ws: (Backward, GapEdge) + (Forward, WordEdge)
        start = motionWordEndpoint(cursor, lines, false, EdgeType::GapEdge, isBigWord, false, leftColOffset, hasLinesAbove);
        end = motionWordEndpoint(cursor, lines, true, EdgeType::WordEdge, isBigWord, false, rightColOffset, hasLinesBelow);
      }
    }
  }

  // Return Range where start/end could be POSITION_OUTSIDE_BOUNDARY
  return Range(start, end);
}

// =============================================================================
// Paragraph endpoint/range computation
// =============================================================================

int motionParagraphEndpoint(int cursorLine,
                            const Lines& lines,
                            bool forward,
                            LineEdgeType edgeType,
                            int boundaryLine) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return 0;

  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isBlankLineStr(lines[cursorLine]);

  int result = cursorLine;

  switch (edgeType) {
    case LineEdgeType::BlockEdge: {
      // Edge of current same-type block
      if (forward) {
        result = paragraphEndLine(lines, cursorLine);
      } else {
        result = paragraphStartLine(lines, cursorLine);
      }
      break;
    }

    case LineEdgeType::GapEdge: {
      // Edge of blank line run adjacent to current paragraph
      if (forward) {
        // Find end of trailing blank lines (after current paragraph)
        int blockEnd = paragraphEndLine(lines, cursorLine);
        if (cursorOnBlank) {
          // Already on blanks - return end of blank run
          result = blockEnd;
        } else if (blockEnd + 1 < n && isBlankLineStr(lines[blockEnd + 1])) {
          // Skip past non-blank paragraph, find end of following blank run
          result = paragraphEndLine(lines, blockEnd + 1);
        } else {
          // No trailing blanks
          result = blockEnd;
        }
      } else {
        // Find start of leading blank lines (before current paragraph)
        int blockStart = paragraphStartLine(lines, cursorLine);
        if (cursorOnBlank) {
          // Already on blanks - return start of blank run
          result = blockStart;
        } else if (blockStart > 0 && isBlankLineStr(lines[blockStart - 1])) {
          // Skip past non-blank paragraph, find start of preceding blank run
          result = paragraphStartLine(lines, blockStart - 1);
        } else {
          // No leading blanks
          result = blockStart;
        }
      }
      break;
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
          result = n - 1;
        } else {
          // Scan forward for next blank line
          i++;
          while (i < n && !isBlankLineStr(lines[i])) {
            i++;
          }
          // Return blank line, or last line if not found
          result = (i < n) ? i : n - 1;
        }
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
        result = max(i, 0);
      }
      break;
    }
  }

  // Check if result crosses boundary
  if (boundaryLine >= 0) {
    if (forward && result >= boundaryLine) {
      return -1;  // Outside boundary
    }
    if (!forward && result <= boundaryLine) {
      return -1;  // Outside boundary
    }
  }

  return result;
}

LineRange paragraphTextObjectRange(int cursorLine,
                                   const Lines& lines,
                                   bool isInner,
                                   int topBoundary,
                                   int bottomBoundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return LINE_RANGE_OUTSIDE_BOUNDARY;

  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isBlankLineStr(lines[cursorLine]);

  int startLine, endLine;

  if (isInner) {
    // dip: (Backward, BlockEdge) + (Forward, BlockEdge)
    startLine = motionParagraphEndpoint(cursorLine, lines, false, LineEdgeType::BlockEdge);
    endLine = motionParagraphEndpoint(cursorLine, lines, true, LineEdgeType::BlockEdge);
  } else if (cursorOnBlank) {
    // dap on blank line: (Backward, BlockEdge) + select blank run + following paragraph
    startLine = motionParagraphEndpoint(cursorLine, lines, false, LineEdgeType::BlockEdge);

    // For "ap on blank", we want blank lines + following paragraph.
    int blankEnd = motionParagraphEndpoint(cursorLine, lines, true, LineEdgeType::BlockEdge);
    if (blankEnd + 1 < n) {
      // There's a non-blank paragraph after - include it
      endLine = motionParagraphEndpoint(blankEnd + 1, lines, true, LineEdgeType::BlockEdge);
    } else {
      // No paragraph after, just the blank lines
      endLine = blankEnd;
    }
  } else {
    // Cursor on non-blank line
    int blockEnd = motionParagraphEndpoint(cursorLine, lines, true, LineEdgeType::BlockEdge);

    // Check for trailing blank lines
    bool hasTrailingBlanks = (blockEnd + 1 < n && isBlankLineStr(lines[blockEnd + 1]));

    if (hasTrailingBlanks) {
      // Has trailing blank lines: (Backward, BlockEdge) + (Forward, GapEdge)
      startLine = motionParagraphEndpoint(cursorLine, lines, false, LineEdgeType::BlockEdge);
      endLine = motionParagraphEndpoint(cursorLine, lines, true, LineEdgeType::GapEdge);
    } else {
      // No trailing blanks: (Backward, GapEdge) + (Forward, BlockEdge)
      startLine = motionParagraphEndpoint(cursorLine, lines, false, LineEdgeType::GapEdge);
      endLine = motionParagraphEndpoint(cursorLine, lines, true, LineEdgeType::BlockEdge);
    }
  }

  // Check if result crosses boundaries
  if (topBoundary >= 0 && startLine <= topBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  if (bottomBoundary >= 0 && endLine >= bottomBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }

  return LineRange(startLine, endLine);
}

// =============================================================================
// Sentence endpoint/range computation
// =============================================================================

// Helper: compute sentence edge without boundary checking
static Position motionSentenceEdgeCore(Position cursor,
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

Position motionSentenceEndpoint(Position cursor,
                                const Lines& lines,
                                bool forward,
                                SentenceEdgeType edgeType,
                                Position boundary) {
  Position result = motionSentenceEdgeCore(cursor, lines, forward, edgeType);

  // Check if result crosses boundary
  if (boundary.isValid()) {
    if (forward && result >= boundary) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    if (!forward && result <= boundary) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
  }

  return result;
}

Range sentenceTextObjectRange(Position cursor,
                              const Lines& lines,
                              bool isInner,
                              Position leftBoundary,
                              Position rightBoundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return RANGE_OUTSIDE_BOUNDARY;

  // Find sentence start (beginning of current sentence)
  auto [startLine, startCol] = findCurrentSentenceStart(lines, cursor.line, cursor.col);

  // Find sentence end by searching forward from sentence start
  Position sentenceStart(startLine, startCol);
  Position sentenceEnd = motionSentenceEndpoint(sentenceStart, lines, true, SentenceEdgeType::SentenceEdge);

  Position resultStart, resultEnd;

  if (isInner) {
    // dis: just the sentence content
    resultStart = sentenceStart;
    resultEnd = sentenceEnd;
  } else {
    // das: include trailing whitespace (or leading if no trailing)
    Position gapEnd = motionSentenceEndpoint(sentenceStart, lines, true, SentenceEdgeType::GapEdge);

    // Check if there's trailing whitespace/blank lines
    bool hasTrailing = (gapEnd.line > sentenceEnd.line ||
                        (gapEnd.line == sentenceEnd.line && gapEnd.col > sentenceEnd.col));

    if (hasTrailing) {
      // Include trailing whitespace
      resultStart = sentenceStart;
      resultEnd = gapEnd;
    } else {
      // No trailing whitespace - include leading whitespace
      // Find gap edge backward from sentence start
      Position gapStart = motionSentenceEndpoint(sentenceStart, lines, false, SentenceEdgeType::GapEdge);

      // Check if there's leading whitespace
      bool hasLeading = (gapStart.line < sentenceStart.line ||
                         (gapStart.line == sentenceStart.line && gapStart.col < sentenceStart.col));

      if (hasLeading) {
        resultStart = gapStart;
        resultEnd = sentenceEnd;
      } else {
        // No surrounding whitespace, just return sentence
        resultStart = sentenceStart;
        resultEnd = sentenceEnd;
      }
    }
  }

  // Check if result crosses boundaries
  if (leftBoundary.isValid() && resultStart <= leftBoundary) {
    return RANGE_OUTSIDE_BOUNDARY;
  }
  if (rightBoundary.isValid() && resultEnd >= rightBoundary) {
    return RANGE_OUTSIDE_BOUNDARY;
  }

  return Range(resultStart, resultEnd);
}

// =============================================================================
// Line endpoint/range computation
// =============================================================================

int motionLineEndpoint(Position cursor,
                       const Lines& lines,
                       bool forward,
                       const EditBoundary& boundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return COL_OUTSIDE_BOUNDARY;

  int line = std::clamp(cursor.line, 0, n - 1);
  int lineLen = static_cast<int>(lines[line].size());

  if (forward) {
    if (line == n - 1 && boundary.hasSuffix()) {
      return COL_OUTSIDE_BOUNDARY;
    }
    return lineLen > 0 ? lineLen - 1 : 0;
  } else {
    if (line == 0 && boundary.hasPrefix()) {
      return COL_OUTSIDE_BOUNDARY;
    }
    return 0;
  }
}

LineRange lineDeleteRange(Position cursor,
                          const Lines& lines,
                          const EditBoundary& boundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return LINE_RANGE_OUTSIDE_BOUNDARY;

  int line = std::clamp(cursor.line, 0, n - 1);

  // Middle lines are always safe, check first / last
  bool onFirstLine = (line == 0);
  bool onLastLine = (line == n - 1);

  if (onFirstLine && boundary.hasPrefix()) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  if (onLastLine && boundary.hasSuffix()) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }

  return LineRange(line, line);
}

} // namespace VimCore
