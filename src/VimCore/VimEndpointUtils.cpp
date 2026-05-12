#include "VimEndpointUtils.h"
#include "VimCore.h"
#include "VimEditUtils.h"

#include <algorithm>
#include <cassert>
#include <utility>

#include "Boundary/TransformBoundary.h"
#include "Types/CursorPos.h"

using namespace std;

namespace VimCore {

// =============================================================================
// Word endpoint/range computation
// =============================================================================

// Templated version for compile-time dispatch on Forward and Edge
template<bool Forward, EdgeType Edge>
CursorPos motionWordEndpoint(CursorPos cursor, const Lines& lines,
                            bool big, bool skipCurrent,
                            int boundaryOffset, bool hasLinesOutside,
                            bool lineBounded) {
  CursorPos result = motionWordCore<Forward, Edge>(cursor, lines, big, skipCurrent, lineBounded);

  // If motion hit buffer boundary, check what kind of crossing this is
  if (result == POSITION_OUTSIDE_BOUNDARY) {
    if (hasLinesOutside || boundaryOffset > 0) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    return cursor;
  }

  // Check if result is in protected column boundary region
  if (boundaryOffset > 0) {
    int lastLine = lines.lastLine();
    if constexpr (Forward) {
      if (result.line == lastLine &&
          result.col >= lines.getSize(lastLine) - boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    } else {
      if (result.line == 0 && result.col < boundaryOffset) {
        return POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }

  return result;
}

// Explicit instantiations
template CursorPos motionWordEndpoint<true, EdgeType::WordEdge>(CursorPos, const Lines&, bool, bool, int, bool, bool);
template CursorPos motionWordEndpoint<true, EdgeType::GapEdge>(CursorPos, const Lines&, bool, bool, int, bool, bool);
template CursorPos motionWordEndpoint<true, EdgeType::NextEdge>(CursorPos, const Lines&, bool, bool, int, bool, bool);
template CursorPos motionWordEndpoint<false, EdgeType::WordEdge>(CursorPos, const Lines&, bool, bool, int, bool, bool);
template CursorPos motionWordEndpoint<false, EdgeType::GapEdge>(CursorPos, const Lines&, bool, bool, int, bool, bool);
template CursorPos motionWordEndpoint<false, EdgeType::NextEdge>(CursorPos, const Lines&, bool, bool, int, bool, bool);

// Runtime dispatch version (for compatibility)
CursorPos motionWordEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                            EdgeType edgeType, bool big, bool skipCurrent,
                            int boundaryOffset, bool hasLinesOutside,
                            bool lineBounded) {
  if (forward) {
    switch (edgeType) {
      case EdgeType::WordEdge:
        return motionWordEndpoint<true, EdgeType::WordEdge>(cursor, lines, big, skipCurrent, boundaryOffset, hasLinesOutside, lineBounded);
      case EdgeType::GapEdge:
        return motionWordEndpoint<true, EdgeType::GapEdge>(cursor, lines, big, skipCurrent, boundaryOffset, hasLinesOutside, lineBounded);
      case EdgeType::NextEdge:
        return motionWordEndpoint<true, EdgeType::NextEdge>(cursor, lines, big, skipCurrent, boundaryOffset, hasLinesOutside, lineBounded);
      default: __builtin_unreachable();
    }
  } else {
    switch (edgeType) {
      case EdgeType::WordEdge:
        return motionWordEndpoint<false, EdgeType::WordEdge>(cursor, lines, big, skipCurrent, boundaryOffset, hasLinesOutside, lineBounded);
      case EdgeType::GapEdge:
        return motionWordEndpoint<false, EdgeType::GapEdge>(cursor, lines, big, skipCurrent, boundaryOffset, hasLinesOutside, lineBounded);
      case EdgeType::NextEdge:
        return motionWordEndpoint<false, EdgeType::NextEdge>(cursor, lines, big, skipCurrent, boundaryOffset, hasLinesOutside, lineBounded);
      default: __builtin_unreachable();
    }
  }
}

CursorPos wordEndpointToRangeEnd(CursorPos endpoint,
                                         const Lines& lines,
                                         EdgeType edgeType) {
  if (endpoint == POSITION_OUTSIDE_BOUNDARY) return endpoint;
  switch (edgeType) {
    case EdgeType::WordEdge:
    case EdgeType::GapEdge:
      return VimCore::onePastOnSameLine(lines, endpoint);
    case EdgeType::NextEdge:
      return endpoint;
    default: __builtin_unreachable();
  }
}

// Helper to compute whitespace run range. Always returns valid position.
// Used for iw/iW when cursor on whitespace
static CharRange computeWhitespaceRun(CursorPos cursor, const Lines& lines) {
  CursorPos start = cursor;
  CursorPos next = lines.getPrevPos(start);
  while (next != start && isBlank(lines.get(next))) {
    start = next;
    next = lines.getPrevPos(start);
  }

  CursorPos end = cursor;
  next = lines.getNextPos(end);
  while (next != end && isBlank(lines.get(next))) {
    end = next;
    next = lines.getNextPos(end);
  }
  return CharRange(start, VimCore::onePastOnSameLine(lines, end));
}

CharRange textObjectCore(CursorPos cursor, const Lines& lines, bool isInner,
                     bool isBigWord) {

  unsigned char c = lines.get(cursor);
  bool cursorOnWhitespace = isBlank(c);

  CursorPos start, end;

  if (isInner) {
    // diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
    if (cursorOnWhitespace) {
      return computeWhitespaceRun(cursor, lines);
    } else {
      // Cursor on word/symbol - use motionWordCore directly
      start = motionWordCore<false, EdgeType::WordEdge>(cursor, lines, isBigWord, false);
      end = wordEndpointToRangeEnd(
          motionWordCore<true, EdgeType::WordEdge>(cursor, lines, isBigWord, false),
          lines, EdgeType::WordEdge);
    }
  } else {
    // daw/daW: depends on cursor position and trailing whitespace
    if (cursorOnWhitespace) {
      // Cursor in whitespace: (Backward, GapEdge) + (Forward, WordEdge)
      // lineBounded=true: don't cross newline backward from indentation
      start = motionWordCore<false, EdgeType::GapEdge>(cursor, lines, isBigWord, false, /*lineBounded=*/true);
      end = wordEndpointToRangeEnd(
          motionWordCore<true, EdgeType::WordEdge>(cursor, lines, isBigWord, false),
          lines, EdgeType::WordEdge);
    } else {
      // Cursor in word/symbol: check for trailing whitespace (NOT newline!)
      CursorPos wordEnd = motionWordCore<true, EdgeType::WordEdge>(cursor, lines, isBigWord, false);
      CursorPos wordStart = motionWordCore<false, EdgeType::WordEdge>(cursor, lines, isBigWord, false);

      bool hasTrailingWs = false;
      if (wordEnd != POSITION_OUTSIDE_BOUNDARY) {
        // Check for space/tab AFTER word on SAME LINE (newline doesn't count)
        int nextCol = wordEnd.col + 1;
        if (nextCol < static_cast<int>(lines[wordEnd.line].size())) {
          hasTrailingWs = isWhitespace(lines[wordEnd.line][nextCol]);
        }
      }

      if (hasTrailingWs) {
        // Has trailing ws: (Backward, WordEdge) + (Forward, GapEdge)
        // lineBounded=true: trailing whitespace doesn't cross lines
        start = wordStart;
        end = wordEndpointToRangeEnd(
            motionWordCore<true, EdgeType::GapEdge>(cursor, lines, isBigWord, false, /*lineBounded=*/true),
            lines, EdgeType::GapEdge);
      } else {
        // No trailing ws: include leading whitespace ONLY if there's a word
        // before on same line. Use lineBounded backward GapEdge - if it returns
        // col 0 or crosses lines, there's only indentation before the word.
        CursorPos gapStart = motionWordCore<false, EdgeType::GapEdge>(cursor, lines, isBigWord, false, /*lineBounded=*/true);
        if (gapStart != POSITION_OUTSIDE_BOUNDARY &&
            gapStart.line == cursor.line && gapStart.col > 0) {
          // Word before on same line: include leading whitespace
          start = gapStart;
        } else {
          // At indentation, line start, or no word found: just use word boundaries
          start = wordStart;
        }
        end = wordEndpointToRangeEnd(wordEnd, lines, EdgeType::WordEdge);
      }
    }
  }

  // Return half-open [begin, end), where invalid endpoints propagate as sentinels.
  if (!start.isValid() || !end.isValid()) {
    return CharRange(start, end);
  }
  return CharRange(start, end);
}

CharRange textObject(CursorPos cursor, const Lines& lines, bool isInner,
                 bool isBigWord) {

  CharRange range = textObjectCore(cursor, lines, isInner, isBigWord);

  // Clamp POSITION_OUTSIDE_BOUNDARY to buffer edges
  if (range.begin == POSITION_OUTSIDE_BOUNDARY) {
    range.begin = CursorPos(0, 0);
  }
  if (range.end == POSITION_OUTSIDE_BOUNDARY) {
    range.end = lines.endPos();
  }

  return range;
}

// daw has exceptions to normal boundary rules, as only places where we call motionWordEndpoint with lineBounded = true
CharRange textObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                      bool isBigWord, int leftColOffset, int rightColOffset,
                      bool hasLinesAbove, bool hasLinesBelow) {

  unsigned char c = lines.get(cursor);
  bool cursorOnWhitespace = isBlank(c);

  CursorPos start, end;

  if (isInner) {
    // diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
    if (cursorOnWhitespace) {
      // Whitespace run doesn't use motionWordEndpoint, so no line crossing
      // possible But we still need to check column boundaries on the result
      CharRange wsRange = computeWhitespaceRun(cursor, lines);
      start = wsRange.begin;
      end = wsRange.end;

      // Check left boundary
      if (leftColOffset > 0 && start.line == 0 && start.col < leftColOffset) {
        start = POSITION_OUTSIDE_BOUNDARY;
      }
      // Check right boundary
      if (rightColOffset > 0) {
        CursorPos tail = lines.getPrevPos(end);
        if (tail == POSITION_OUTSIDE_BOUNDARY) {
          end = POSITION_OUTSIDE_BOUNDARY;
        } else {
          int lastLine = lines.lastLine();
          if (tail.line == lastLine) {
            int lineLen = static_cast<int>(lines[lastLine].size());
            if (tail.col >= lineLen - rightColOffset) {
              end = POSITION_OUTSIDE_BOUNDARY;
            }
          }
        }
      }

      // Whitespace-run path is already half-open.
      return CharRange(start, end);
    } else {
      // Cursor on word/symbol - use WordEdge motions WITH boundary checking
      start = motionWordEndpoint<false, EdgeType::WordEdge>(
          cursor, lines, isBigWord, false, leftColOffset, hasLinesAbove);
      end = wordEndpointToRangeEnd(
          motionWordEndpoint<true, EdgeType::WordEdge>(
              cursor, lines, isBigWord, false, rightColOffset, hasLinesBelow),
          lines, EdgeType::WordEdge);
    }
  } else {
    // daw/daW: depends on cursor position and trailing whitespace
    if (cursorOnWhitespace) {
      // Cursor in whitespace: (Backward, GapEdge) + (Forward, WordEdge)
      // lineBounded=true: don't cross newline backward from indentation
      start = motionWordEndpoint<false, EdgeType::GapEdge>(
          cursor, lines, isBigWord, false, leftColOffset, hasLinesAbove, /*lineBounded=*/true);
      CursorPos wordEnd = motionWordEndpoint<true, EdgeType::WordEdge>(
          cursor, lines, isBigWord, false, rightColOffset, hasLinesBelow);
      end = wordEndpointToRangeEnd(wordEnd, lines, EdgeType::WordEdge);

      // If forward motion ended on whitespace, no word was found - signal
      // invalid
      if (wordEnd != POSITION_OUTSIDE_BOUNDARY && isBlank(lines.get(wordEnd))) {
        end = POSITION_OUTSIDE_BOUNDARY;
      }
    } else {
      // Cursor in word/symbol: check for trailing whitespace (NOT newline!)
      CursorPos wordEnd = motionWordEndpoint<true, EdgeType::WordEdge>(
          cursor, lines, isBigWord, false, 0, false);
      CursorPos wordStart = motionWordEndpoint<false, EdgeType::WordEdge>(
          cursor, lines, isBigWord, false, 0, false);

      bool hasTrailingWs = false;
      if (wordEnd != POSITION_OUTSIDE_BOUNDARY) {
        // Check for space/tab AFTER word on SAME LINE (newline doesn't count)
        int nextCol = wordEnd.col + 1;
        if (nextCol < static_cast<int>(lines[wordEnd.line].size())) {
          hasTrailingWs = isWhitespace(lines[wordEnd.line][nextCol]);
        }
      }

      if (hasTrailingWs) {
        // Has trailing ws: (Backward, WordEdge) + (Forward, GapEdge)
        // lineBounded=true: trailing whitespace doesn't cross lines
        start = motionWordEndpoint<false, EdgeType::WordEdge>(
            cursor, lines, isBigWord, false, leftColOffset, hasLinesAbove);
        end = wordEndpointToRangeEnd(
            motionWordEndpoint<true, EdgeType::GapEdge>(
                cursor, lines, isBigWord, false, rightColOffset, hasLinesBelow, /*lineBounded=*/true),
            lines, EdgeType::GapEdge);
      } else {
        // No trailing ws: include leading whitespace ONLY if there's a word
        // before on same line. Use lineBounded backward GapEdge - if it returns
        // col 0 or crosses lines, there's only indentation before the word.
        CursorPos gapStart = motionWordEndpoint<false, EdgeType::GapEdge>(
            cursor, lines, isBigWord, false, leftColOffset, hasLinesAbove, /*lineBounded=*/true);
        if (gapStart != POSITION_OUTSIDE_BOUNDARY &&
            gapStart.line == cursor.line && gapStart.col > 0) {
          // Word before on same line: include leading whitespace
          start = gapStart;
        } else {
          // Check if we're rejecting because boundary clips the whitespace.
          // If whitespace exists before the word but falls in the prefix
          // boundary, Vim's `aw` would still include it — reject to avoid
          // producing a range that disagrees with actual Vim behavior.
          CursorPos wordStart = motionWordEndpoint<false, EdgeType::WordEdge>(
              cursor, lines, isBigWord, false, leftColOffset, hasLinesAbove);
          if (wordStart != POSITION_OUTSIDE_BOUNDARY &&
              wordStart.line == cursor.line && wordStart.col > 0 &&
              isBlank(lines[wordStart.line][wordStart.col - 1])) {
            start = POSITION_OUTSIDE_BOUNDARY;
          } else {
            start = wordStart;
          }
        }
        end = wordEndpointToRangeEnd(
            motionWordEndpoint<true, EdgeType::WordEdge>(
                cursor, lines, isBigWord, false, rightColOffset, hasLinesBelow),
            lines, EdgeType::WordEdge);
      }
    }
  }

  // Return half-open [begin, end), where invalid endpoints propagate as sentinels.
  if (!start.isValid() || !end.isValid()) {
    return CharRange(start, end);
  }
  return CharRange(start, end);
}

static bool crossesColumnBoundary(CharRange range, const Lines& lines,
                                  int leftColOffset, int rightColOffset) {
  if (!range.isValid()) return false;
  range.normalize();

  if (leftColOffset > 0 && range.begin.line == 0 &&
      range.begin.col < leftColOffset) {
    return true;
  }

  if (rightColOffset > 0 && range.end.line == lines.lastLine()) {
    int firstSuffixCol = static_cast<int>(lines[range.end.line].size()) - rightColOffset;
    if (range.end.col > firstSuffixCol) return true;
  }

  return false;
}

struct QuoteSpan {
  static constexpr int NONE = -1;

  int open = NONE;
  int close = NONE;

  bool found() const { return open != NONE; }
  bool valid() const { return found() && open < close; }
  bool contains(int col) const { return open <= col && col <= close; }
  bool startsAfter(int col) const { return open > col; }
};

static QuoteSpan findQuoteSpan(const string& line, char quote, int col) {
  QuoteSpan span;

  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] != quote) continue;

    if (!span.found()) {
      span.open = i;
      continue;
    }

    span.close = i;
    assert(span.valid());
    if (span.contains(col) || span.startsAfter(col)) return span;
    span = QuoteSpan{};
  }

  return {};
}

CharRange quoteTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                               char quote, int leftColOffset, int rightColOffset) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return CharRange(cursor, cursor);

  int line = std::clamp(cursor.line, 0, n - 1);
  const string& ln = lines[line];
  int len = static_cast<int>(ln.size());
  if (len == 0) return CharRange(cursor, cursor);

  int col = std::clamp(cursor.col, 0, len - 1);
  QuoteSpan span = findQuoteSpan(ln, quote, col);
  if (!span.valid()) return CharRange(cursor, cursor);

  CharRange range = isInner
      ? CharRange(CursorPos(line, span.open + 1), CursorPos(line, span.close))
      : CharRange(CursorPos(line, span.open), CursorPos(line, span.close + 1));
  if (range.isEmpty()) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  if (crossesColumnBoundary(range, lines, leftColOffset, rightColOffset)) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  return range;
}

static pair<CursorPos, CursorPos> findMatchingBrackets(
    const Lines& lines, CursorPos pos, char open, char close) {
  int n = static_cast<int>(lines.size());
  CursorPos openPos(-1, -1);
  CursorPos closePos(-1, -1);

  int line = pos.line;
  int col = pos.col;
  int depth = 0;
  bool cursorOnClose = false;
  if (line >= 0 && line < n && col >= 0 &&
      col < static_cast<int>(lines[line].size())) {
    char c = lines[line][col];
    if (c == open) depth = 1;
    else if (c == close) cursorOnClose = true;
  }

  int searchLine = pos.line;
  int searchCol = pos.col - (cursorOnClose ? 1 : 0);
  if (depth == 0) {
    depth = cursorOnClose ? 1 : 0;
    while (searchLine >= 0) {
      const string& ln = lines[searchLine];
      int startCol = searchLine == pos.line
          ? searchCol
          : static_cast<int>(ln.size()) - 1;

      for (int c = startCol; c >= 0; c--) {
        if (ln[c] == close) {
          depth++;
        } else if (ln[c] == open) {
          if (depth == 0) {
            openPos = CursorPos(searchLine, c);
            goto foundOpen;
          }
          depth--;
          if (cursorOnClose && depth == 0) {
            openPos = CursorPos(searchLine, c);
            goto foundOpen;
          }
        }
      }
      searchLine--;
    }
  } else {
    openPos = CursorPos(line, col);
  }

foundOpen:
  if (openPos.line < 0) return {CursorPos(-1, -1), CursorPos(-1, -1)};

  depth = 1;
  searchLine = openPos.line;
  searchCol = openPos.col + 1;

  while (searchLine < n) {
    const string& ln = lines[searchLine];
    int startCol = searchLine == openPos.line ? searchCol : 0;

    for (int c = startCol; c < static_cast<int>(ln.size()); c++) {
      if (ln[c] == open) {
        depth++;
      } else if (ln[c] == close) {
        depth--;
        if (depth == 0) {
          closePos = CursorPos(searchLine, c);
          return {openPos, closePos};
        }
      }
    }
    searchLine++;
  }

  return {CursorPos(-1, -1), CursorPos(-1, -1)};
}

CharRange bracketTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                                 char open, char close, int leftColOffset,
                                 int rightColOffset) {
  auto [openPos, closePos] = findMatchingBrackets(lines, cursor, open, close);
  if (openPos.line < 0 || closePos.line < 0) return CharRange(cursor, cursor);

  CharRange range = isInner
      ? CharRange(lines.getNextPos(openPos), closePos)
      : CharRange(openPos, VimCore::onePastOnSameLine(lines, closePos));
  if (range.isEmpty()) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  if (crossesColumnBoundary(range, lines, leftColOffset, rightColOffset)) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  return range;
}

// =============================================================================
// Paragraph endpoint/range computation
// =============================================================================

// Core implementation - computes endpoint without boundary checking
template<bool Forward, LineEdgeType Edge>
static int motionParagraphEndpointCore(int cursorLine, const Lines& lines) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return 0;

  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isBlankLineStr(lines[cursorLine]);

  int result = cursorLine;

  if constexpr (Edge == LineEdgeType::BlockEdge) {
    // Edge of current same-type block
    if constexpr (Forward) {
      result = paragraphEndLine(lines, cursorLine);
    } else {
      result = paragraphStartLine(lines, cursorLine);
    }
  } else if constexpr (Edge == LineEdgeType::GapEdge) {
    // Edge of blank line run adjacent to current paragraph
    if constexpr (Forward) {
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
  } else if constexpr (Edge == LineEdgeType::NextEdge) {
    // Edge of next block (}/{ motions go to blank line separator)
    if constexpr (Forward) {
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
  }

  return result;
}

// Templated version for compile-time dispatch on Forward and Edge
template<bool Forward, LineEdgeType Edge>
int motionParagraphEndpoint(int cursorLine, const Lines& lines, bool hasLinesOutside) {
  int result = motionParagraphEndpointCore<Forward, Edge>(cursorLine, lines);

  // Boundary check
  if (hasLinesOutside) {
    int lastLine = static_cast<int>(lines.size()) - 1;
    if constexpr (Forward) {
      if (result >= lastLine) {
        return LINE_OUTSIDE_BOUNDARY;
      }
    } else {
      if (result <= 0) {
        return LINE_OUTSIDE_BOUNDARY;
      }
    }
  }

  return result;
}

// Explicit instantiations for templated version
template int motionParagraphEndpoint<true, LineEdgeType::BlockEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<true, LineEdgeType::GapEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<true, LineEdgeType::NextEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<false, LineEdgeType::BlockEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<false, LineEdgeType::GapEdge>(int, const Lines&, bool);
template int motionParagraphEndpoint<false, LineEdgeType::NextEdge>(int, const Lines&, bool);

// Runtime dispatch version (for internal use in text object functions)
int motionParagraphEndpoint(int cursorLine, const Lines& lines, bool forward,
                            LineEdgeType edgeType) {
  if (forward) {
    switch (edgeType) {
      case LineEdgeType::BlockEdge:
        return motionParagraphEndpointCore<true, LineEdgeType::BlockEdge>(cursorLine, lines);
      case LineEdgeType::GapEdge:
        return motionParagraphEndpointCore<true, LineEdgeType::GapEdge>(cursorLine, lines);
      case LineEdgeType::NextEdge:
        return motionParagraphEndpointCore<true, LineEdgeType::NextEdge>(cursorLine, lines);
      default: __builtin_unreachable();
    }
  } else {
    switch (edgeType) {
      case LineEdgeType::BlockEdge:
        return motionParagraphEndpointCore<false, LineEdgeType::BlockEdge>(cursorLine, lines);
      case LineEdgeType::GapEdge:
        return motionParagraphEndpointCore<false, LineEdgeType::GapEdge>(cursorLine, lines);
      case LineEdgeType::NextEdge:
        return motionParagraphEndpointCore<false, LineEdgeType::NextEdge>(cursorLine, lines);
      default: __builtin_unreachable();
    }
  }
}

LineRange paragraphTextObjectRange(int cursorLine, const Lines& lines,
                                   bool isInner, int topBoundary,
                                   int bottomBoundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return LINE_RANGE_OUTSIDE_BOUNDARY;

  cursorLine = std::clamp(cursorLine, 0, n - 1);
  bool cursorOnBlank = isBlankLineStr(lines[cursorLine]);

  int startLine;
  int endLine;

  if (isInner) {
    // dip: (Backward, BlockEdge) + (Forward, BlockEdge)
    startLine = motionParagraphEndpoint(cursorLine, lines, false,
                                        LineEdgeType::BlockEdge);
    int blockEndLine = motionParagraphEndpoint(cursorLine, lines, true,
                                               LineEdgeType::BlockEdge);
    endLine = blockEndLine + 1;
  } else if (cursorOnBlank) {
    // dap on blank line: (Backward, BlockEdge) + select blank run + following
    // paragraph
    startLine = motionParagraphEndpoint(cursorLine, lines, false,
                                        LineEdgeType::BlockEdge);

    // For "ap on blank", we want blank lines + following paragraph.
    int blankEnd = motionParagraphEndpoint(cursorLine, lines, true,
                                           LineEdgeType::BlockEdge);
    if (blankEnd + 1 < n) {
      // There's a non-blank paragraph after - include it
      int followingBlockEnd = motionParagraphEndpoint(blankEnd + 1, lines, true,
                                                      LineEdgeType::BlockEdge);
      endLine = followingBlockEnd + 1;
    } else {
      // No paragraph after, just the blank lines
      endLine = blankEnd + 1;
    }
  } else {
    // Cursor on non-blank line
    int blockEnd = motionParagraphEndpoint(cursorLine, lines, true,
                                           LineEdgeType::BlockEdge);

    // Check for trailing blank lines
    bool hasTrailingBlanks =
        (blockEnd + 1 < n && isBlankLineStr(lines[blockEnd + 1]));

    if (hasTrailingBlanks) {
      // Has trailing blank lines: (Backward, BlockEdge) + (Forward, GapEdge)
      startLine = motionParagraphEndpoint(cursorLine, lines, false,
                                          LineEdgeType::BlockEdge);
      int gapEndLine = motionParagraphEndpoint(cursorLine, lines, true,
                                               LineEdgeType::GapEdge);
      endLine = gapEndLine + 1;
    } else {
      // No trailing blanks: (Backward, GapEdge) + (Forward, BlockEdge)
      startLine = motionParagraphEndpoint(cursorLine, lines, false,
                                          LineEdgeType::GapEdge);
      int blockEndLine = motionParagraphEndpoint(cursorLine, lines, true,
                                                 LineEdgeType::BlockEdge);
      endLine = blockEndLine + 1;
    }
  }

  // Check if result crosses boundaries
  if (topBoundary >= 0 && startLine <= topBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  if (bottomBoundary >= 0 && endLine > bottomBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }

  return LineRange(startLine, endLine);
}

// =============================================================================
// Sentence endpoint/range computation
// =============================================================================

// Helper: compute sentence edge without boundary checking
static CursorPos motionSentenceEdgeCore(CursorPos cursor, const Lines& lines,
                                       bool forward,
                                       SentenceEdgeType edgeType) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return cursor;

  int line = std::clamp(cursor.line, 0, n - 1);
  int col =
      lines[line].empty()
          ? 0
          : std::clamp(cursor.col, 0, static_cast<int>(lines[line].size()) - 1);

  if (forward) {
    // Forward motion - find next sentence boundary

    // If on blank line, skip to first non-blank line
    if (isBlankLineStr(lines[line])) {
      while (line < n && isBlankLineStr(lines[line])) {
        line++;
      }
      if (line >= n)
        return CursorPos(n - 1, 0);

      // For NextEdge: return start of first non-blank line (sentence start)
      if (edgeType == SentenceEdgeType::NextEdge) {
        return CursorPos(line, firstNonBlankColInLineStr(lines[line]));
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
          // At EOF after sentence-ending punctuation.
          // For NextEdge: return one-past-end so exclusive d) includes the punctuation.
          // For SentenceEdge/GapEdge: return the punctuation position itself.
          if (edgeType == SentenceEdgeType::NextEdge)
            return CursorPos(endLine, endCol + 1);
          return CursorPos(endLine, endCol);
        }

        // Skip closers on same line
        while (true) {
          unsigned char c = getChar(lines, l, k);
          if (!isSentenceCloser(c))
            break;
          endLine = l;
          endCol = k;
          int tl = l, tk = k;
          if (!stepFwd(lines, tl, tk))
            break;
          if (tl != l)
            break;
          l = tl;
          k = tk;
        }

        // For SentenceEdge: return position of last closer (or punctuation)
        if (edgeType == SentenceEdgeType::SentenceEdge) {
          return CursorPos(endLine, endCol);
        }

        // Now skip whitespace and blank lines to find gap edge or next sentence
        int gapEndLine = l, gapEndCol = k;

        while (true) {
          if (l >= n)
            break;
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
            if (!stepFwd(lines, l, k))
              break;
            continue;
          }
          // Found non-blank
          break;
        }

        if (edgeType == SentenceEdgeType::GapEdge) {
          return CursorPos(gapEndLine, gapEndCol);
        }

        // NextEdge: return start of next sentence
        if (l >= n)
          return CursorPos(n - 1, 0);
        return CursorPos(l, k);
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
        return CursorPos(sl, sc);
      }
    }

    // For NextEdge (( motion): return sentence start
    if (edgeType == SentenceEdgeType::NextEdge) {
      return CursorPos(sl, sc);
    }

    // For SentenceEdge: find the sentence end before this sentence start
    // Go back from sentence start to find the previous sentence's end
    if (sl == 0 && sc == 0) {
      // At buffer start
      return CursorPos(0, 0);
    }

    int l = sl, k = sc;
    if (!stepBack(lines, l, k)) {
      return CursorPos(0, 0);
    }

    // Skip whitespace/blank lines backward to find gap start or sentence end
    while (true) {
      if (isBlankLineStr(lines[l])) {
        if (edgeType == SentenceEdgeType::GapEdge) {
          // Find start of blank run
          while (l > 0 && isBlankLineStr(lines[l - 1])) {
            l--;
          }
          return CursorPos(l, 0);
        }
        // Skip blank lines
        while (l > 0 && isBlankLineStr(lines[l])) {
          l--;
        }
        if (isBlankLineStr(lines[l])) {
          // All blank lines to start
          return CursorPos(0, 0);
        }
        k = static_cast<int>(lines[l].size()) - 1;
        if (k < 0)
          k = 0;
        continue;
      }

      unsigned char c = getChar(lines, l, k);
      if (c == ' ' || c == '\t') {
        if (edgeType == SentenceEdgeType::GapEdge) {
          // Skip forward past whitespace to find gap end
          while (true) {
            unsigned char nc = getChar(lines, l, k);
            if (nc != ' ' && nc != '\t')
              break;
            int tl = l, tk = k;
            if (!stepFwd(lines, tl, tk))
              break;
            if (tl != l)
              break;
            l = tl;
            k = tk;
          }
          // Step back to last whitespace
          if (!stepBack(lines, l, k))
            return CursorPos(0, 0);
          return CursorPos(l, k);
        }
        // Skip whitespace backward
        if (!stepBack(lines, l, k)) {
          return CursorPos(0, 0);
        }
        continue;
      }

      // Found non-whitespace - this should be sentence end (closer or
      // punctuation) Skip closers backward
      while (isSentenceCloser(c)) {
        if (!stepBack(lines, l, k)) {
          return CursorPos(l, k);
        }
        c = getChar(lines, l, k);
      }

      // Should be at punctuation mark
      if (c == '.' || c == '!' || c == '?') {
        return CursorPos(l, k);
      }

      // If not at sentence-ending punctuation, continue backward
      if (!stepBack(lines, l, k)) {
        return CursorPos(0, 0);
      }
    }
  }
}

// Templated version for compile-time dispatch on Forward and Edge
template<bool Forward, SentenceEdgeType Edge>
CursorPos motionSentenceEndpoint(CursorPos cursor, const Lines& lines,
                                int boundaryOffset, bool hasLinesOutside) {
  CursorPos result = motionSentenceEdgeCore(cursor, lines, Forward, Edge);

  // Boundary check (same pattern as motionWordEndpoint)
  int lastLine = lines.lastLine();
  if constexpr (Forward) {
    // Check line boundary
    if (hasLinesOutside && result.line >= lastLine) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    // Check column boundary on last line
    if (boundaryOffset > 0 && result.line == lastLine &&
        result.col >= static_cast<int>(lines[lastLine].size()) - boundaryOffset) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
  } else {
    // Check line boundary
    if (hasLinesOutside && result.line <= 0) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    // Check column boundary on line 0
    if (boundaryOffset > 0 && result.line == 0 && result.col < boundaryOffset) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
  }

  return result;
}

// Explicit instantiations for templated version
template CursorPos motionSentenceEndpoint<true, SentenceEdgeType::SentenceEdge>(CursorPos, const Lines&, int, bool);
template CursorPos motionSentenceEndpoint<true, SentenceEdgeType::GapEdge>(CursorPos, const Lines&, int, bool);
template CursorPos motionSentenceEndpoint<true, SentenceEdgeType::NextEdge>(CursorPos, const Lines&, int, bool);
template CursorPos motionSentenceEndpoint<false, SentenceEdgeType::SentenceEdge>(CursorPos, const Lines&, int, bool);
template CursorPos motionSentenceEndpoint<false, SentenceEdgeType::GapEdge>(CursorPos, const Lines&, int, bool);
template CursorPos motionSentenceEndpoint<false, SentenceEdgeType::NextEdge>(CursorPos, const Lines&, int, bool);

// Runtime dispatch version (for internal use in text object functions)
CursorPos motionSentenceEndpoint(CursorPos cursor, const Lines& lines,
                                bool forward, SentenceEdgeType edgeType) {
  return motionSentenceEdgeCore(cursor, lines, forward, edgeType);
}

CursorPos sentenceEndpointToRangeEnd(CursorPos endpoint,
                                             const Lines& lines,
                                             SentenceEdgeType edgeType) {
  if (endpoint == POSITION_OUTSIDE_BOUNDARY) return endpoint;
  switch (edgeType) {
    case SentenceEdgeType::SentenceEdge:
    case SentenceEdgeType::GapEdge:
      return VimCore::onePastOnSameLine(lines, endpoint);
    case SentenceEdgeType::NextEdge:
      return endpoint;
    default: __builtin_unreachable();
  }
}

CharRange sentenceTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                              CursorPos leftBoundary, CursorPos rightBoundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return CHAR_RANGE_OUTSIDE_BOUNDARY;

  // Find sentence start (beginning of current sentence)
  auto [startLine, startCol] =
      findCurrentSentenceStart(lines, cursor.line, cursor.col);

  // Find sentence end by searching forward from sentence start
  CursorPos sentenceStart(startLine, startCol);
  CursorPos sentenceEnd = sentenceEndpointToRangeEnd(
      motionSentenceEndpoint(sentenceStart, lines, true, SentenceEdgeType::SentenceEdge),
      lines, SentenceEdgeType::SentenceEdge);

  CursorPos resultStart, resultEnd;

  if (isInner) {
    // dis: just the sentence content
    resultStart = sentenceStart;
    resultEnd = sentenceEnd;
  } else {
    // das: include trailing whitespace (or leading if no trailing)
    CursorPos gapEnd = sentenceEndpointToRangeEnd(
        motionSentenceEndpoint(sentenceStart, lines, true, SentenceEdgeType::GapEdge),
        lines, SentenceEdgeType::GapEdge);

    // Check if there's trailing whitespace/blank lines.
    bool hasTrailing = gapEnd > sentenceEnd;

    if (hasTrailing) {
      // Include trailing whitespace
      resultStart = sentenceStart;
      resultEnd = gapEnd;
    } else {
      // No trailing whitespace - include leading whitespace
      // Find gap edge backward from sentence start
      CursorPos gapStart = motionSentenceEndpoint(sentenceStart, lines, false,
                                                 SentenceEdgeType::GapEdge);

      // Check if there's leading whitespace
      bool hasLeading = (gapStart.line < sentenceStart.line ||
                         (gapStart.line == sentenceStart.line &&
                          gapStart.col < sentenceStart.col));

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

  // Check if result crosses boundaries using half-open semantics.
  if (leftBoundary.isValid() && resultStart <= leftBoundary) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }
  if (rightBoundary.isValid() && resultEnd > rightBoundary) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }

  return CharRange(resultStart, resultEnd);
}

// =============================================================================
// Scroll endpoint computation
// =============================================================================

int scrollEndpoint(int cursorLine, int numLines, int shift, bool hasLinesAbove,
                   bool hasLinesBelow) {
  if (numLines == 0)
    return LINE_OUTSIDE_BOUNDARY;

  int lastLine = numLines - 1;
  int targetLine = cursorLine + shift;

  // Clamp to buffer bounds
  targetLine = std::clamp(targetLine, 0, lastLine);

  // Check if motion would escape bounds
  if (shift > 0) {
    // Down scroll: suspicious if landing on or past last line when there are
    // lines below
    if (hasLinesBelow && targetLine >= lastLine) {
      return LINE_OUTSIDE_BOUNDARY;
    }
  } else if (shift < 0) {
    // Up scroll: suspicious if landing on or before first line when there are
    // lines above
    if (hasLinesAbove && targetLine <= 0) {
      return LINE_OUTSIDE_BOUNDARY;
    }
  }

  return targetLine;
}

// =============================================================================
// Line endpoint/range computation
// =============================================================================

int motionLineEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                       const TransformBoundary& boundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return COL_OUTSIDE_BOUNDARY;

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

LineRange lineDeleteRange(CursorPos cursor, const Lines& lines,
                          const TransformBoundary& boundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return LINE_RANGE_OUTSIDE_BOUNDARY;

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

  return LineRange(line, line + 1);
}

} // namespace VimCore
