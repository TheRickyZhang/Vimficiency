#include "VimEndpointUtils.h"
#include "CharMask.h"
#include "VimCore.h"
#include "VimEditUtils.h"
#include "VimMotionUtils.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

#include "Boundary/TransformBoundary.h"
#include "Types/CursorPos.h"

using namespace std;

namespace VimCore {

// =============================================================================
// Word endpoint/range computation
// =============================================================================

static bool hasWordBoundary(WordBoundaryContext boundary) {
  return boundary.leftColOffset > 0 || boundary.rightColOffset > 0 ||
         boundary.hasLinesAbove || boundary.hasLinesBelow;
}

static bool usesWordEndpointBoundarySemantics(WordBoundaryContext boundary) {
  return hasWordBoundary(boundary) || !boundary.clampOutside;
}

static bool inWordBoundaryRegion(const CursorPos& pos, const Lines& lines,
                                 WordBoundaryContext boundary) {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;
  if (pos.line == 0 && pos.col < boundary.leftColOffset) return true;
  return boundary.inSuffixRegion(pos, lines);
}

template<bool Forward, EdgeType Edge>
struct WordScan {
  static constexpr bool forward = Forward;
  static constexpr EdgeType edge = Edge;
  bool skipCurrent = false;
  bool lineBounded = false;
};

static constexpr WordScan<true, EdgeType::NextEdge> kScanNextBegin{
    false, false};
static constexpr WordScan<true, EdgeType::WordEdge> kScanNextEnd{
    true, false};
static constexpr WordScan<true, EdgeType::GapEdge> kScanNextGap{
    false, false};
static constexpr WordScan<false, EdgeType::WordEdge> kScanPreviousBegin{
    true, false};
static constexpr WordScan<false, EdgeType::NextEdge> kScanPreviousEnd{
    true, false};
static constexpr WordScan<false, EdgeType::GapEdge> kScanPreviousGap{
    false, false};

template<typename Scan>
static constexpr Scan lineBounded(Scan scan) {
  scan.lineBounded = true;
  return scan;
}

template<typename Scan>
static constexpr Scan noSkip(Scan scan) {
  scan.skipCurrent = false;
  return scan;
}

template<typename Scan>
static int wordBoundaryOffset(Scan, WordBoundaryContext boundary) {
  return Scan::forward ? boundary.rightColOffset : boundary.leftColOffset;
}

template<typename Scan>
static bool wordHasLinesOutside(Scan, WordBoundaryContext boundary) {
  return Scan::forward ? boundary.hasLinesBelow : boundary.hasLinesAbove;
}

template<typename Scan>
static CursorPos rawWordEndpoint(CursorPos cursor, const Lines& lines,
                                 bool big, Scan scan) {
  return motionWordCore<Scan::forward, Scan::edge>(
      cursor, lines, big, scan.skipCurrent, scan.lineBounded);
}

template<typename Scan>
static CursorPos boundedWordEndpoint(CursorPos cursor,
                                     const Lines& lines,
                                     bool big,
                                     Scan scan,
                                     WordBoundaryContext boundary) {
  CursorPos result = rawWordEndpoint(cursor, lines, big, scan);

  int boundaryOffset = wordBoundaryOffset(scan, boundary);
  bool hasLinesOutside = wordHasLinesOutside(scan, boundary);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    if (hasLinesOutside || boundaryOffset > 0 || !boundary.clampOutside) {
      return POSITION_OUTSIDE_BOUNDARY;
    }
    return cursor;
  }

  if (boundaryOffset > 0) {
    int lastLine = lines.lastLine();
    if constexpr (Scan::forward) {
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

template<typename Scan>
static CursorPos wordTextObjectEndpoint(CursorPos cursor,
                                        const Lines& lines,
                                        bool big,
                                        Scan scan,
                                        WordBoundaryContext boundary) {
  if (!usesWordEndpointBoundarySemantics(boundary)) {
    return rawWordEndpoint(cursor, lines, big, scan);
  }
  return boundedWordEndpoint(cursor, lines, big, scan, boundary);
}

static CursorPos onePastInclusiveWordEndpoint(CursorPos endpoint,
                                              const Lines& lines) {
  if (endpoint == POSITION_OUTSIDE_BOUNDARY) return endpoint;
  return VimCore::onePastOnSameLine(lines, endpoint);
}

static CharRange computeWhitespaceRun(CursorPos cursor, const Lines& lines) {
  const string& line = lines[cursor.line];
  if (line.empty()) return CharRange(cursor, cursor);

  CursorPos start = cursor;
  while (start.col > 0 &&
         CharMask::isBlank(line[start.col - 1])) {
    start.col--;
  }

  CursorPos end = cursor;
  while (end.col + 1 < static_cast<int>(line.size()) &&
         CharMask::isBlank(line[end.col + 1])) {
    end.col++;
  }
  return CharRange(start, VimCore::onePastOnSameLine(lines, end));
}

static CharRange applyWhitespaceRunBoundary(CharRange range,
                                            const Lines& lines,
                                            WordBoundaryContext boundary) {
  if (boundary.leftColOffset > 0 && range.begin.line == 0 &&
      range.begin.col < boundary.leftColOffset) {
    range.begin = POSITION_OUTSIDE_BOUNDARY;
  }

  if (boundary.rightColOffset > 0) {
    CursorPos tail = lines.getPrevPos(range.end);
    if (tail == POSITION_OUTSIDE_BOUNDARY) {
      range.end = POSITION_OUTSIDE_BOUNDARY;
    } else if (tail.line == lines.lastLine()) {
      int lineLen = static_cast<int>(lines[lines.lastLine()].size());
      if (tail.col >= lineLen - boundary.rightColOffset) {
        range.end = POSITION_OUTSIDE_BOUNDARY;
      }
    }
  }
  return range;
}

static CharRange finalizeWordTextObjectRange(CharRange range,
                                             const Lines& lines,
                                             WordBoundaryContext boundary) {
  if (!usesWordEndpointBoundarySemantics(boundary)) {
    if (range.begin == POSITION_OUTSIDE_BOUNDARY) range.begin = CursorPos(0, 0);
    if (range.end == POSITION_OUTSIDE_BOUNDARY) range.end = lines.endPos();
  }
  return range;
}

CursorPos wordMotionEndpoint(CursorPos cursor,
                             const Lines& lines,
                             WordMotionTarget target,
                             bool isBigWord,
                             WordBoundaryContext boundary) {
  switch (target) {
    case WordMotionTarget::NextBegin:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, kScanNextBegin, boundary);
    case WordMotionTarget::NextEnd:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, kScanNextEnd, boundary);
    case WordMotionTarget::PreviousBegin:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, kScanPreviousBegin, boundary);
    case WordMotionTarget::PreviousEnd:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, kScanPreviousEnd, boundary);
  }
  __builtin_unreachable();
}

CharRange wordOperatorRange(CursorPos cursor,
                            const Lines& lines,
                            WordOperatorTarget target,
                            bool isBigWord,
                            WordBoundaryContext boundary) {
  CursorPos endpoint = POSITION_OUTSIDE_BOUNDARY;

  switch (target) {
    case WordOperatorTarget::DeleteToNextWord:
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, kScanNextGap, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      if (endpoint.line > cursor.line) {
        return CharRange(
            cursor,
            CursorPos(cursor.line, static_cast<int>(lines[cursor.line].size())));
      }
      return CharRange(cursor, onePastInclusiveWordEndpoint(endpoint, lines));

    case WordOperatorTarget::DeleteToWordEnd:
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, kScanNextEnd, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return CharRange(cursor, onePastInclusiveWordEndpoint(endpoint, lines));

    case WordOperatorTarget::DeleteBackToWordBegin: {
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, kScanPreviousBegin, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      int contentStartCol = boundary.contentStartCol(cursor.line);
      if (cursor.col == contentStartCol && endpoint.line == cursor.line) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return buildBackwardExclusiveCharRange(
          endpoint, cursor, lines, contentStartCol);
    }

    case WordOperatorTarget::DeleteBackToWordEnd:
      if (inWordBoundaryRegion(cursor, lines, boundary)) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, kScanPreviousEnd, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return CharRange(endpoint, VimCore::onePastOnSameLine(lines, cursor));
  }

  __builtin_unreachable();
}

CharRange wordTextObjectRange(CursorPos cursor,
                              const Lines& lines,
                              WordTextObjectKind kind,
                              bool isBigWord,
                              WordBoundaryContext boundary) {
  bool isInner = kind == WordTextObjectKind::Inner;
  bool cursorOnWhitespace = CharMask::isBlank(lines.get(cursor));
  CursorPos start;
  CursorPos end;

  if (isInner && cursorOnWhitespace) {
    return finalizeWordTextObjectRange(
        applyWhitespaceRunBoundary(computeWhitespaceRun(cursor, lines),
                                   lines, boundary),
        lines, boundary);
  }

  if (isInner) {
    start = wordTextObjectEndpoint(
        cursor, lines, isBigWord, noSkip(kScanPreviousBegin), boundary);
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, noSkip(kScanNextEnd), boundary),
        lines);
    return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
  }

  if (cursorOnWhitespace) {
    start = wordTextObjectEndpoint(
        cursor, lines, isBigWord, lineBounded(kScanPreviousGap),
        boundary);
    CursorPos wordEnd = wordTextObjectEndpoint(
        cursor, lines, isBigWord, noSkip(kScanNextEnd), boundary);
    end = onePastInclusiveWordEndpoint(wordEnd, lines);
    if (wordEnd != POSITION_OUTSIDE_BOUNDARY &&
        CharMask::isBlank(lines.get(wordEnd))) {
      end = POSITION_OUTSIDE_BOUNDARY;
    }
    return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
  }

  CursorPos rawWordEnd = rawWordEndpoint(
      cursor, lines, isBigWord, noSkip(kScanNextEnd));
  bool hasTrailingWhitespace = false;
  if (rawWordEnd != POSITION_OUTSIDE_BOUNDARY) {
    int nextCol = rawWordEnd.col + 1;
    if (nextCol < static_cast<int>(lines[rawWordEnd.line].size())) {
      hasTrailingWhitespace =
          CharMask::isWhitespace(lines[rawWordEnd.line][nextCol]);
    }
  }

  if (hasTrailingWhitespace) {
    start = wordTextObjectEndpoint(
        cursor, lines, isBigWord, noSkip(kScanPreviousBegin), boundary);
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, lineBounded(kScanNextGap), boundary),
        lines);
  } else {
    CursorPos gapStart = wordTextObjectEndpoint(
        cursor, lines, isBigWord, lineBounded(kScanPreviousGap),
        boundary);
    if (gapStart != POSITION_OUTSIDE_BOUNDARY &&
        gapStart.line == cursor.line && gapStart.col > 0) {
      start = gapStart;
    } else {
      CursorPos wordStart = wordTextObjectEndpoint(
          cursor, lines, isBigWord, noSkip(kScanPreviousBegin), boundary);
      if (wordStart != POSITION_OUTSIDE_BOUNDARY &&
          wordStart.line == cursor.line && wordStart.col > 0 &&
          CharMask::isBlank(lines[wordStart.line][wordStart.col - 1])) {
        start = POSITION_OUTSIDE_BOUNDARY;
      } else {
        start = wordStart;
      }
    }
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, noSkip(kScanNextEnd), boundary),
        lines);
  }

  return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
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
  bool cursorOnBlank = isBlankLine(lines[cursorLine]);

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
      } else if (blockEnd + 1 < n && isBlankLine(lines[blockEnd + 1])) {
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
      } else if (blockStart > 0 && isBlankLine(lines[blockStart - 1])) {
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
      while (i < n && isBlankLine(lines[i])) {
        i++;
      }
      if (i >= n) {
        // All blanks to end - return last line
        result = n - 1;
      } else {
        // Scan forward for next blank line
        i++;
        while (i < n && !isBlankLine(lines[i])) {
          i++;
        }
        // Return blank line, or last line if not found
        result = (i < n) ? i : n - 1;
      }
    } else {
      // Skip current blank lines
      int i = cursorLine;
      while (i > 0 && isBlankLine(lines[i])) {
        i--;
      }
      // Scan backward for previous blank line
      i--;
      while (i >= 0 && !isBlankLine(lines[i])) {
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
  bool cursorOnBlank = isBlankLine(lines[cursorLine]);

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
        (blockEnd + 1 < n && isBlankLine(lines[blockEnd + 1]));

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

namespace {

struct SentenceExtent {
  CursorPos start;
  CursorPos sentenceEndExclusive;
  CursorPos trailingEndExclusive;
  CursorPos operatorEndpoint;
};

CursorPos clampSentencePos(CursorPos cursor, const Lines& lines) {
  int line = std::clamp(cursor.line, 0, lines.lastLine());
  int col = lines[line].empty()
      ? 0
      : std::clamp(cursor.col, 0, static_cast<int>(lines[line].size()) - 1);
  return CursorPos(line, col);
}

CursorPos onePastSentenceEnd(const Lines& lines, CursorPos endInclusive) {
  return VimCore::onePastOnSameLine(lines, endInclusive);
}

CursorPos firstSentenceStartAfterBlankRun(const Lines& lines, int line) {
  int n = static_cast<int>(lines.size());
  while (line < n && isBlankLine(lines[line])) {
    line++;
  }
  if (line >= n) return CursorPos(n - 1, 0);
  return CursorPos(line, firstNonBlankColInLine(lines[line]));
}

CursorPos currentSentenceStart(CursorPos cursor, const Lines& lines) {
  CursorPos clamped = clampSentencePos(cursor, lines);
  auto [line, col] = findCurrentSentenceStart(lines, clamped.line, clamped.col);
  return CursorPos(line, col);
}

CursorPos previousSentenceStart(CursorPos sentenceStart, const Lines& lines) {
  CursorPos clamped = clampSentencePos(sentenceStart, lines);
  int line = clamped.line;
  int col = clamped.col;
  if (!stepBack(lines, line, col)) return clamped;

  auto [prevLine, prevCol] = findCurrentSentenceStart(lines, line, col);
  CursorPos previous(prevLine, prevCol);
  return previous < clamped ? previous : clamped;
}

std::optional<CursorPos> sentenceGapEndpoint(CursorPos cursor, const Lines& lines) {
  CursorPos clamped = clampSentencePos(cursor, lines);
  char c = getChar(lines, clamped.line, clamped.col);
  if (c != ' ' && c != '\t') return std::nullopt;

  int l = clamped.line;
  int k = clamped.col;
  if (!stepBack(lines, l, k)) return std::nullopt;

  while (true) {
    char prev = getChar(lines, l, k);
    if (!CharMask::isWhitespace(prev)) break;
    if (!stepBack(lines, l, k)) return std::nullopt;
  }

  while (CharMask::isSentenceCloser(getChar(lines, l, k))) {
    if (!stepBack(lines, l, k)) return std::nullopt;
  }

  if (!isSentenceEndAt(lines, l, k)) return std::nullopt;

  l = clamped.line;
  k = clamped.col;
  while (true) {
    char gap = getChar(lines, l, k);
    if (!CharMask::isWhitespace(gap)) return CursorPos(l, k);
    if (!stepFwd(lines, l, k)) {
      return onePastSentenceEnd(lines, clamped);
    }
  }
}

std::optional<SentenceExtent> sentenceExtentAtOrAfter(CursorPos cursor,
                                                      const Lines& lines) {
  if (lines.empty()) return std::nullopt;

  CursorPos scanStart = clampSentencePos(cursor, lines);
  int line = scanStart.line;
  int col = scanStart.col;
  if (isBlankLine(lines[line])) {
    scanStart = firstSentenceStartAfterBlankRun(lines, line);
    line = scanStart.line;
    col = scanStart.col;
  }

  CursorPos start = currentSentenceStart(scanStart, lines);
  int l = line;
  int k = col;

  while (true) {
    if (isSentenceEndAt(lines, l, k)) {
      int endLine = l;
      int endCol = k;

      if (!stepFwd(lines, l, k)) {
        CursorPos sentenceEndExclusive =
            onePastSentenceEnd(lines, CursorPos(endLine, endCol));
        return SentenceExtent{
            start, sentenceEndExclusive, sentenceEndExclusive, sentenceEndExclusive};
      }

      while (true) {
        char c = getChar(lines, l, k);
        if (!CharMask::isSentenceCloser(c)) break;
        endLine = l;
        endCol = k;
        int tl = l;
        int tk = k;
        if (!stepFwd(lines, tl, tk)) break;
        if (tl != l) break;
        l = tl;
        k = tk;
      }

      CursorPos sentenceEndExclusive =
          onePastSentenceEnd(lines, CursorPos(endLine, endCol));
      CursorPos gapEndInclusive(l, k);
      bool gapReachedEof = false;

      while (true) {
        if (l >= static_cast<int>(lines.size())) break;
        if (isBlankLine(lines[l])) {
          gapEndInclusive = CursorPos(l, 0);
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
        char c = lines[l][k];
        if (c == ' ' || c == '\t') {
          gapEndInclusive = CursorPos(l, k);
          if (!stepFwd(lines, l, k)) {
            gapReachedEof = true;
            break;
          }
          continue;
        }
        break;
      }

      CursorPos trailingEndExclusive = onePastSentenceEnd(lines, gapEndInclusive);
      CursorPos operatorEndpoint = gapReachedEof
          ? trailingEndExclusive
          : l >= static_cast<int>(lines.size())
          ? CursorPos(lines.lastLine(), 0)
          : CursorPos(l, k);
      return SentenceExtent{
          start, sentenceEndExclusive, trailingEndExclusive, operatorEndpoint};
    }

    if (!stepFwd(lines, l, k)) return std::nullopt;
  }
}

std::optional<SentenceExtent> sentenceExtentContaining(CursorPos cursor,
                                                       const Lines& lines) {
  if (lines.empty()) return std::nullopt;
  return sentenceExtentAtOrAfter(currentSentenceStart(cursor, lines), lines);
}

}  // namespace

static bool sentenceEndpointOutsideBoundary(CursorPos result, const Lines& lines,
                                            bool forward, int boundaryOffset,
                                            bool hasLinesOutside) {
  int lastLine = lines.lastLine();
  if (forward) {
    if (hasLinesOutside && result.line >= lastLine) {
      return true;
    }
    if (boundaryOffset > 0 && result.line == lastLine &&
        result.col >= static_cast<int>(lines[lastLine].size()) - boundaryOffset) {
      return true;
    }
  } else {
    if (hasLinesOutside && result.line <= 0) {
      return true;
    }
    if (boundaryOffset > 0 && result.line == 0 && result.col < boundaryOffset) {
      return true;
    }
  }
  return false;
}

CursorPos sentenceMotionEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                                 int boundaryOffset, bool hasLinesOutside) {
  if (lines.empty()) return cursor;

  CursorPos result = cursor;
  if (forward) {
    motionSentenceNext(result, lines);
  } else {
    motionSentencePrev(result, lines);
  }

  if (sentenceEndpointOutsideBoundary(
          result, lines, forward, boundaryOffset, hasLinesOutside)) {
    return POSITION_OUTSIDE_BOUNDARY;
  }
  return result;
}

static CursorPos sentenceOperatorEndpointCore(CursorPos cursor, const Lines& lines,
                                              bool forward) {
  if (lines.empty()) return cursor;

  CursorPos clamped = clampSentencePos(cursor, lines);

  if (forward) {
    if (isBlankLine(lines[clamped.line])) {
      return firstSentenceStartAfterBlankRun(lines, clamped.line);
    }
    if (auto gapEndpoint = sentenceGapEndpoint(clamped, lines)) {
      return *gapEndpoint;
    }
    auto extent = sentenceExtentAtOrAfter(clamped, lines);
    return extent ? extent->operatorEndpoint : cursor;
  }

  CursorPos start = currentSentenceStart(clamped, lines);
  return start == clamped ? previousSentenceStart(start, lines) : start;
}

CursorPos sentenceOperatorEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                                   int boundaryOffset, bool hasLinesOutside) {
  if (lines.empty()) return cursor;
  CursorPos result = sentenceOperatorEndpointCore(cursor, lines, forward);
  if (sentenceEndpointOutsideBoundary(
          result, lines, forward, boundaryOffset, hasLinesOutside)) {
    return POSITION_OUTSIDE_BOUNDARY;
  }
  return result;
}

CharRange sentenceTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                              CursorPos leftBoundary, CursorPos rightBoundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return CHAR_RANGE_OUTSIDE_BOUNDARY;

  auto extent = sentenceExtentContaining(cursor, lines);
  if (!extent) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  CursorPos resultStart, resultEnd;

  if (isInner) {
    resultStart = extent->start;
    resultEnd = extent->sentenceEndExclusive;
  } else {
    bool hasTrailing = extent->trailingEndExclusive > extent->sentenceEndExclusive;

    if (hasTrailing) {
      resultStart = extent->start;
      resultEnd = extent->trailingEndExclusive;
    } else {
      CursorPos previousStart = previousSentenceStart(extent->start, lines);
      auto previous = previousStart < extent->start
          ? sentenceExtentAtOrAfter(previousStart, lines)
          : std::nullopt;

      if (previous && previous->sentenceEndExclusive < extent->start) {
        resultStart = previous->sentenceEndExclusive;
        resultEnd = extent->sentenceEndExclusive;
      } else {
        resultStart = extent->start;
        resultEnd = extent->sentenceEndExclusive;
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
