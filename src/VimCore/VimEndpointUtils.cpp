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

static constexpr WordScan<true, EdgeType::NextEdge> SCAN_NEXT_BEGIN{
    false, false};
static constexpr WordScan<true, EdgeType::WordEdge> SCAN_NEXT_END{
    true, false};
static constexpr WordScan<true, EdgeType::GapEdge> SCAN_NEXT_GAP{
    false, false};
static constexpr WordScan<false, EdgeType::WordEdge> SCAN_PREVIOUS_BEGIN{
    true, false};
static constexpr WordScan<false, EdgeType::NextEdge> SCAN_PREVIOUS_END{
    true, false};
static constexpr WordScan<false, EdgeType::GapEdge> SCAN_PREVIOUS_GAP{
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

static CursorPos includeFollowingEmptyLineAfterTrailingGap(
    CursorPos end, const Lines& lines) {
  if (end == POSITION_OUTSIDE_BOUNDARY) return end;
  if (end.line >= lines.lastLine()) return end;
  if (end.col != static_cast<int>(lines[end.line].size())) return end;
  if (!lines[end.line + 1].empty()) return end;
  return CursorPos(end.line + 1, 0);
}

CursorPos wordMotionEndpoint(CursorPos cursor,
                             const Lines& lines,
                             WordMotionTarget target,
                             bool isBigWord,
                             WordBoundaryContext boundary) {
  switch (target) {
    case WordMotionTarget::NextBegin:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_NEXT_BEGIN, boundary);
    case WordMotionTarget::NextEnd:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_NEXT_END, boundary);
    case WordMotionTarget::PreviousBegin:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_BEGIN, boundary);
    case WordMotionTarget::PreviousEnd:
      return boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_END, boundary);
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
          cursor, lines, isBigWord, SCAN_NEXT_GAP, boundary);
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
          cursor, lines, isBigWord, SCAN_NEXT_END, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return CharRange(cursor, onePastInclusiveWordEndpoint(endpoint, lines));

    case WordOperatorTarget::DeleteBackToWordBegin: {
      endpoint = boundedWordEndpoint(
          cursor, lines, isBigWord, SCAN_PREVIOUS_BEGIN, boundary);
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
          cursor, lines, isBigWord, SCAN_PREVIOUS_END, boundary);
      if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor) {
        return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
      }
      return CharRange(endpoint, VimCore::onePastOnSameLine(lines, cursor));
  }

  __builtin_unreachable();
}

static CharRange wordTextObjectRangeImpl(CursorPos cursor,
                                         const Lines& lines,
                                         WordTextObjectKind kind,
                                         bool isBigWord,
                                         WordBoundaryContext boundary,
                                         bool includeTrailingEmptyLine) {
  bool isInner = kind == WordTextObjectKind::Inner;

  // When no boundary context constraints apply, route to the faithful
  // current_word port. This gives correct Vim semantics for `aw`/`iw`/`aW`/
  // `iW` in the common case. Boundary-aware callers (TransformExplorer) fall
  // through to the existing implementation which handles boundary clipping.
  if (!usesWordEndpointBoundarySemantics(boundary)) {
    bool include = !isInner;
    auto cw = currentWord(cursor, lines, include, isBigWord);
    if (!cw.ok) {
      return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
    }
    // Vim's current_word can return start > end when the algorithm scanned
    // backward (e.g. cursor on trailing empty line with fwd_word + decl).
    // The operator treats the range as the span [min, max] inclusive.
    CursorPos lo = cw.start <= cw.end ? cw.start : cw.end;
    CursorPos hi = cw.start <= cw.end ? cw.end : cw.start;
    CharRange range(lo, VimCore::onePastOnSameLine(lines, hi));

    // Vim operator-level quirk: `daw` (around, delete-shaped) on a buffer
    // where the range starts on a blank line and the end-line's trailing
    // whitespace reaches EOL extends to include the following empty line.
    // Not in current_word per se — surfaces only at operator application.
    bool startsOnBlankLine =
        lo.line >= 0 && lo.line < static_cast<int>(lines.size()) &&
        isBlankLine(lines[lo.line]);
    bool endsAtLineTrailingWhitespace = false;
    if (range.end.line >= 0 && range.end.line < static_cast<int>(lines.size())) {
      const string& endLine = lines[range.end.line];
      int endCol = range.end.col;
      // The exclusive end is at or before EOL of its line, and everything
      // from endCol to EOL is whitespace. (If endCol >= size, the range
      // doesn't end in trailing whitespace — it's already past EOL.)
      if (endCol < static_cast<int>(endLine.size())) {
        bool allWhitespace = true;
        for (int c = endCol; c < static_cast<int>(endLine.size()); c++) {
          if (!CharMask::isWhitespace(endLine[c])) { allWhitespace = false; break; }
        }
        endsAtLineTrailingWhitespace = allWhitespace;
      }
    }
    if (!isInner && includeTrailingEmptyLine &&
        startsOnBlankLine && endsAtLineTrailingWhitespace) {
      // Push range.end to one-past-EOL first so includeFollowing... sees the
      // "at EOL" precondition it expects.
      if (range.end.line >= 0 && range.end.line < static_cast<int>(lines.size())) {
        range.end.col = static_cast<int>(lines[range.end.line].size());
      }
      range.end = includeFollowingEmptyLineAfterTrailingGap(range.end, lines);
    }

    // Vim op_delete extension: when the inclusive end of a text-object range
    // is on the NUL of an empty line, the delete operator treats the range
    // as consuming the empty line. Combined with blank-prefix begin, this
    // satisfies the exclusive-linewise rule. Implement via snapping begin.col
    // to 0 so applyCharDeletionToBuffer's auto-empty-line-removal does the
    // linewise effect. Only fires for delete-shaped (includeTrailingEmptyLine);
    // change-shaped operator (c<text-obj>) keeps the original characterwise
    // range — Vim preserves leading whitespace in that case.
    if (includeTrailingEmptyLine &&
        range.end.line > range.begin.line &&
        range.end.line < static_cast<int>(lines.size()) &&
        lines[range.end.line].empty() &&
        range.end.col == 0 &&
        range.begin.col > 0 &&
        hasOnlyBlankPrefix(lines[range.begin.line], range.begin.col)) {
      range.begin.col = 0;
      if (range.end.line + 1 < static_cast<int>(lines.size())) {
        range.end = CursorPos(range.end.line + 1, 0);
      }
    }

    return finalizeWordTextObjectRange(range, lines, boundary);
  }

  bool cursorOnWhitespace = CharMask::isBlank(lines.get(cursor));
  CursorPos start;
  CursorPos end;

  if (lines[cursor.line].empty()) {
    // `iw`/`iW` on trailing empty line folds in the previous line (matches
    // Vim for the common cases). `aw`/`aW` is always a no-op on empty line.
    // Note: Vim's full `current_word` has more nuanced conditional folding
    // we don't model — particularly for multi-line buffers with intermediate
    // content; primitive test narrows to avoid those cases.
    if (isInner &&
        cursor.line == lines.lastLine() &&
        cursor.line > 0 &&
        !boundary.hasLinesBelow) {
      return finalizeWordTextObjectRange(
          CharRange(CursorPos(cursor.line - 1, 0), cursor), lines, boundary);
    }
    return CharRange(cursor, cursor);
  }

  if (isInner && cursorOnWhitespace) {
    return finalizeWordTextObjectRange(
        applyWhitespaceRunBoundary(computeWhitespaceRun(cursor, lines),
                                   lines, boundary),
        lines, boundary);
  }

  if (isInner) {
    start = wordTextObjectEndpoint(
        cursor, lines, isBigWord, noSkip(SCAN_PREVIOUS_BEGIN), boundary);
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary),
        lines);
    return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
  }

  if (cursorOnWhitespace) {
    CharRange whitespace = applyWhitespaceRunBoundary(
        computeWhitespaceRun(cursor, lines), lines, boundary);
    if (!whitespace.isValid()) {
      return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
    }

    if (whitespace.end.col < static_cast<int>(lines[whitespace.end.line].size())) {
      CursorPos wordEnd = wordTextObjectEndpoint(
          whitespace.end, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary);
      end = onePastInclusiveWordEndpoint(wordEnd, lines);
    } else if (whitespace.end.line < lines.lastLine()) {
      int nextLine = whitespace.end.line + 1;
      if (isParagraphSeparatorLine(lines[nextLine])) {
        end = CursorPos(nextLine, 0);
      } else if (isBlankLine(lines[nextLine])) {
        end = POSITION_OUTSIDE_BOUNDARY;
      } else {
        CursorPos wordStart(
            nextLine, firstNonBlankColInLine(lines[nextLine]));
        CursorPos rawWordEnd = rawWordEndpoint(
            wordStart, lines, isBigWord, noSkip(SCAN_NEXT_END));
        bool hasTrailingWhitespace = false;
        bool trailingWhitespaceReachesLineEnd = false;
        if (rawWordEnd != POSITION_OUTSIDE_BOUNDARY) {
          int nextCol = rawWordEnd.col + 1;
          if (nextCol < static_cast<int>(lines[rawWordEnd.line].size())) {
            hasTrailingWhitespace =
                CharMask::isWhitespace(lines[rawWordEnd.line][nextCol]);
            trailingWhitespaceReachesLineEnd = hasTrailingWhitespace;
            for (int col = nextCol + 1;
                 trailingWhitespaceReachesLineEnd &&
                 col < static_cast<int>(lines[rawWordEnd.line].size());
                 col++) {
              trailingWhitespaceReachesLineEnd =
                  CharMask::isWhitespace(lines[rawWordEnd.line][col]);
            }
          }
        }
        if (hasTrailingWhitespace &&
            trailingWhitespaceReachesLineEnd &&
            includeTrailingEmptyLine &&
            isBlankLine(lines[whitespace.begin.line])) {
          end = onePastInclusiveWordEndpoint(
              wordTextObjectEndpoint(
                  wordStart, lines, isBigWord, lineBounded(SCAN_NEXT_GAP),
                  boundary),
              lines);
          if (includeTrailingEmptyLine) {
            end = includeFollowingEmptyLineAfterTrailingGap(end, lines);
          }
        } else {
          CursorPos wordEnd = wordTextObjectEndpoint(
              wordStart, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary);
          end = onePastInclusiveWordEndpoint(wordEnd, lines);
        }
      }
    } else {
      end = POSITION_OUTSIDE_BOUNDARY;
    }
    if (end == POSITION_OUTSIDE_BOUNDARY) {
      return CharRange(POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);
    }
    return finalizeWordTextObjectRange(
        CharRange(whitespace.begin, end), lines, boundary);
  }

  CursorPos rawWordEnd = rawWordEndpoint(
      cursor, lines, isBigWord, noSkip(SCAN_NEXT_END));
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
        cursor, lines, isBigWord, noSkip(SCAN_PREVIOUS_BEGIN), boundary);
    end = onePastInclusiveWordEndpoint(
        wordTextObjectEndpoint(
            cursor, lines, isBigWord, lineBounded(SCAN_NEXT_GAP), boundary),
        lines);
  } else {
    CursorPos gapStart = wordTextObjectEndpoint(
        cursor, lines, isBigWord, lineBounded(SCAN_PREVIOUS_GAP),
        boundary);
    if (gapStart != POSITION_OUTSIDE_BOUNDARY &&
        gapStart.line == cursor.line && gapStart.col > 0) {
      start = gapStart;
    } else {
      CursorPos wordStart = wordTextObjectEndpoint(
          cursor, lines, isBigWord, noSkip(SCAN_PREVIOUS_BEGIN), boundary);
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
            cursor, lines, isBigWord, noSkip(SCAN_NEXT_END), boundary),
        lines);
  }

  return finalizeWordTextObjectRange(CharRange(start, end), lines, boundary);
}

CharRange wordTextObjectRange(CursorPos cursor,
                              const Lines& lines,
                              WordTextObjectKind kind,
                              bool isBigWord,
                              WordBoundaryContext boundary) {
  return wordTextObjectRangeImpl(
      cursor, lines, kind, isBigWord, boundary,
      /*includeTrailingEmptyLine=*/true);
}

CharRange wordTextObjectChangeRange(CursorPos cursor,
                                    const Lines& lines,
                                    WordTextObjectKind kind,
                                    bool isBigWord,
                                    WordBoundaryContext boundary) {
  return wordTextObjectRangeImpl(
      cursor, lines, kind, isBigWord, boundary,
      /*includeTrailingEmptyLine=*/false);
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

// Port of Neovim's find_next_quote (textobject.c:1489). Returns -1 if not found.
// Escape character handling (b_p_qe) omitted; we treat all quotes as unescaped.
static int findNextQuote(const string& line, int col, char quote) {
  int len = static_cast<int>(line.size());
  while (col < len) {
    if (line[col] == quote) return col;
    col++;
  }
  return -1;
}

// Port of Neovim's find_prev_quote (textobject.c:1515). Returns 0 if not found
// (caller must check line[result] == quote to distinguish).
static int findPrevQuote(const string& line, int colStart, char quote) {
  while (colStart > 0) {
    colStart--;
    if (line[colStart] == quote) break;
  }
  return colStart;
}

// Port of Neovim's current_quote (textobject.c:1542), non-Visual operator path,
// count == 1. Returns the deletion range as a half-open CharRange.
CharRange quoteTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                               char quote, int leftColOffset, int rightColOffset) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  int lineIdx = std::clamp(cursor.line, 0, n - 1);
  const string& ln = lines[lineIdx];
  int len = static_cast<int>(ln.size());
  if (len == 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  int colStart = std::clamp(cursor.col, 0, len - 1);
  int colEnd = -1;

  if (ln[colStart] == quote) {
    // Cursor on a quote: search line start to find which pair contains it.
    int firstCol = colStart;
    colStart = 0;
    while (true) {
      colStart = findNextQuote(ln, colStart, quote);
      if (colStart < 0 || colStart > firstCol) return CHAR_RANGE_OUTSIDE_BOUNDARY;
      colEnd = findNextQuote(ln, colStart + 1, quote);
      if (colEnd < 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;
      if (colStart <= firstCol && firstCol <= colEnd) break;
      colStart = colEnd + 1;
    }
  } else {
    // Search backward for opening quote, fall back to forward.
    colStart = findPrevQuote(ln, colStart, quote);
    if (colStart >= len || ln[colStart] != quote) {
      colStart = findNextQuote(ln, colStart, quote);
      if (colStart < 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;
    }
    colEnd = findNextQuote(ln, colStart + 1, quote);
    if (colEnd < 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }

  // Around: extend through trailing whitespace, or leading if no trailing.
  if (!isInner) {
    auto isWs = [](char c) { return c == ' ' || c == '\t'; };
    if (colEnd + 1 < len && isWs(ln[colEnd + 1])) {
      while (colEnd + 1 < len && isWs(ln[colEnd + 1])) colEnd++;
    } else {
      while (colStart > 0 && isWs(ln[colStart - 1])) colStart--;
    }
  }

  // Inner with count<2 and no Visual: advance past opening quote.
  if (isInner) colStart++;

  // End is inclusive at colEnd; inc_cursor==2 (past EOL) sets inclusive flag.
  // For around (or inner with quotes adjacent at EOL after inc), motion ends
  // inclusive — half-open is [colStart, colEnd+1). For inner exclusive, it is
  // [colStart, colEnd). For around, inc_cursor on the closing quote: returns 2
  // only if colEnd is at the last char position. We model: around always
  // includes the closing quote (inclusive), inner excludes it.
  int endExclusive = isInner ? colEnd : colEnd + 1;

  CharRange range(CursorPos(lineIdx, colStart), CursorPos(lineIdx, endExclusive));
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
  if (openPos.line < 0) {
    searchLine = std::max(0, pos.line);
    while (searchLine < n) {
      const string& ln = lines[searchLine];
      int startCol = searchLine == pos.line ? std::max(0, pos.col) : 0;

      for (int c = startCol; c < static_cast<int>(ln.size()); c++) {
        if (ln[c] == open) {
          openPos = CursorPos(searchLine, c);
          goto foundOpenForward;
        }
      }
      searchLine++;
    }
  }

foundOpenForward:
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

// Port of Neovim's inc (memline.c). Returns true if advanced.
static bool incPos(const Lines& lines, int& line, int& col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n) return false;
  int len = static_cast<int>(lines[line].size());
  if (col < len) { col++; return true; }
  if (line + 1 < n) { line++; col = 0; return true; }
  return false;
}

// Port of Neovim's incl: advance and skip past NUL positions.
static bool inclPos(const Lines& lines, int& line, int& col) {
  if (!incPos(lines, line, col)) return false;
  if (line < static_cast<int>(lines.size()) &&
      col >= static_cast<int>(lines[line].size())) {
    if (!incPos(lines, line, col)) return false;
  }
  return true;
}

static bool decPos(const Lines& lines, int& line, int& col) {
  if (col > 0) { col--; return true; }
  if (line > 0) {
    line--;
    col = static_cast<int>(lines[line].size());
    return true;
  }
  return false;
}

static bool declPos(const Lines& lines, int& line, int& col) {
  if (!decPos(lines, line, col)) return false;
  if (col >= static_cast<int>(lines[line].size())) {
    if (!decPos(lines, line, col)) return false;
  }
  return true;
}

// Port of Neovim's inindent(extra=1): true iff cols [0, col] are all whitespace.
static bool inIndent(const Lines& lines, int line, int col) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return false;
  const string& ln = lines[line];
  int len = static_cast<int>(ln.size());
  for (int i = 0; i <= col; i++) {
    if (i >= len) return false;
    if (ln[i] != ' ' && ln[i] != '\t') return false;
  }
  return true;
}

CharRange bracketTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                                 char open, char close, int leftColOffset,
                                 int rightColOffset) {
  auto [openPos, closePos] = findMatchingBrackets(lines, cursor, open, close);
  if (openPos.line < 0 || closePos.line < 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  if (!isInner) {
    // Around: simple inclusive range [open, close+1).
    CharRange range(openPos, VimCore::onePastOnSameLine(lines, closePos));
    if (crossesColumnBoundary(range, lines, leftColOffset, rightColOffset)) {
      return CHAR_RANGE_OUTSIDE_BOUNDARY;
    }
    return range;
  }

  // Inner: port of current_block (textobject.c:955). Start is incl past open;
  // cursor is decl from close, skipping leading indent on the close's line.
  int sl = openPos.line, sc = openPos.col;
  if (!inclPos(lines, sl, sc)) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  int cl = closePos.line, cc = closePos.col;
  bool sol = (cc == 0);
  if (!declPos(lines, cl, cc)) return CHAR_RANGE_OUTSIDE_BOUNDARY;
  while (inIndent(lines, cl, cc)) {
    sol = true;
    if (!declPos(lines, cl, cc)) break;
  }

  CharRange range;
  if (sol) {
    // Motion exclusive; advance cursor past indent line boundary.
    int el = cl, ec = cc;
    if (!inclPos(lines, el, ec)) {
      return CHAR_RANGE_OUTSIDE_BOUNDARY;
    }
    range = CharRange(CursorPos(sl, sc), CursorPos(el, ec));
  } else if (CursorPos(sl, sc) <= CursorPos(cl, cc)) {
    // Inclusive end: one past cursor.
    int el = cl, ec = cc;
    incPos(lines, el, ec);
    range = CharRange(CursorPos(sl, sc), CursorPos(el, ec));
  } else {
    // Empty: no text between brackets; cursor lands at start_pos.
    range = CharRange(CursorPos(sl, sc), CursorPos(sl, sc));
  }

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
  bool cursorOnBlank = isParagraphSeparatorLine(lines[cursorLine]);

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
      } else if (blockEnd + 1 < n &&
                 isParagraphSeparatorLine(lines[blockEnd + 1])) {
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
      } else if (blockStart > 0 &&
                 isParagraphSeparatorLine(lines[blockStart - 1])) {
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
      while (i < n && isParagraphSeparatorLine(lines[i])) {
        i++;
      }
      if (i >= n) {
        // All blanks to end - return last line
        result = n - 1;
      } else {
        // Scan forward for next blank line
        i++;
        while (i < n && !isParagraphSeparatorLine(lines[i])) {
          i++;
        }
        // Return blank line, or last line if not found
        result = (i < n) ? i : n - 1;
      }
    } else {
      // Skip current blank lines
      int i = cursorLine;
      while (i > 0 && isParagraphSeparatorLine(lines[i])) {
        i--;
      }
      // Scan backward for previous blank line
      i--;
      while (i >= 0 && !isParagraphSeparatorLine(lines[i])) {
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
  // Faithful port of Neovim's `current_par` (textobject.c) for count=1.
  // Differs from `}`/`{` motion: text object uses `linewhite` (whitespace-only
  // OR empty), motion uses strict empty-line check.
  int n = static_cast<int>(lines.size());
  if (n == 0)
    return LINE_RANGE_OUTSIDE_BOUNDARY;

  auto linewhite = [&](int lnum) {
    if (lnum < 0 || lnum >= n) return false;
    const std::string& line = lines[lnum];
    for (char c : line) {
      if (c != ' ' && c != '\t') return false;
    }
    return true;
  };

  int startLine = std::clamp(cursorLine, 0, n - 1);
  bool whiteInFront = linewhite(startLine);

  // Move start back through same-blankness lines.
  while (startLine > 0 && linewhite(startLine - 1) == whiteInFront) {
    startLine--;
  }

  // Move end forward through same-blankness lines.
  int endLine = startLine;
  while (endLine < n && linewhite(endLine) == whiteInFront) {
    endLine++;
  }
  endLine--;

  bool prevStartIsWhite = whiteInFront;
  bool doWhite = false;
  if (isInner) {
    // `ip`: just the current same-blankness block. No extension.
  } else {
    // At EOF: if current block is white, FAIL (no following non-white para to
    // wrap with). If non-white, skip forward extension and absorb all leading
    // blanks (any kind) backward — Vim's observable behavior for `ap` on a
    // paragraph that runs to buffer end.
    if (endLine >= n - 1) {
      if (whiteInFront) return LINE_RANGE_OUTSIDE_BOUNDARY;
      while (startLine > 0 && linewhite(startLine - 1)) {
        startLine--;
      }
    } else {
      doWhite = true;
      int t = endLine + 1;
      while (t < n) {
        if (linewhite(t) != prevStartIsWhite) {
          // The next block's blankness drives where the second forward-extend
          // loop runs. Cursor-on-white → entering non-white → doWhite=false →
          // extend through the non-white para. Cursor-on-non-white → entering
          // white → doWhite=true → extend through trailing blanks AND include
          // one extra line for the gap before the next non-white block.
          doWhite = linewhite(t);
          if (doWhite) endLine++;
          break;
        }
        t++;
      }
      endLine++;
      while (endLine < n && linewhite(endLine) == doWhite) endLine++;
      endLine--;
      prevStartIsWhite = doWhite;

      // If we didn't wrap a blank gap (forward extension landed on non-white),
      // look for blanks preceding the start.
      if (!whiteInFront && startLine > 0) {
        bool startIsWhite = !prevStartIsWhite;
        while (startLine > 0 &&
               linewhite(startLine - 1) == startIsWhite) {
          startLine--;
          startIsWhite = !startIsWhite;
        }
      }
    }
  }

  int finalEndLine = endLine + 1;
  if (topBoundary >= 0 && startLine <= topBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  if (bottomBoundary >= 0 && finalEndLine > bottomBoundary) {
    return LINE_RANGE_OUTSIDE_BOUNDARY;
  }
  return LineRange(startLine, finalEndLine);
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
  if (line >= n) return POSITION_OUTSIDE_BOUNDARY;
  return CursorPos(line, firstNonBlankColInLine(lines[line]));
}

// Vim's `d)` extends past EOF when forward sentence motion lands on the
// last character of the buffer (unterminated tail). Detect that to convert
// a motion endpoint into the operator endpoint.
bool motionEndsAtLastBufferChar(CursorPos motionEnd, const Lines& lines) {
  int lastLine = lines.lastLine();
  if (motionEnd.line != lastLine) return false;
  int len = static_cast<int>(lines[lastLine].size());
  if (len == 0) return false;
  return motionEnd.col == len - 1;
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
    if (scanStart == POSITION_OUTSIDE_BOUNDARY) return std::nullopt;
    line = scanStart.line;
    col = scanStart.col;
  }

  CursorPos start = currentSentenceStart(scanStart, lines);
  int l = line;
  int k = col;

  while (true) {
    if (isParagraphSeparatorLine(lines[l])) {
      return SentenceExtent{
          start, CursorPos(l, 0), CursorPos(l, 0), CursorPos(l, 0)};
    }

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
        if (!stepFwd(lines, tl, tk)) {
          l = endLine;
          k = static_cast<int>(lines[endLine].size());
          break;
        }
        if (tl != l) {
          l = tl;
          k = tk;
          break;
        }
        l = tl;
        k = tk;
      }

      CursorPos sentenceEndExclusive =
          onePastSentenceEnd(lines, CursorPos(endLine, endCol));
      CursorPos gapEndInclusive(l, k);
      bool gapReachedEof = false;

      // A blank line is a paragraph/sentence boundary in Vim; the trailing
      // gap stops at the start of that line and does not span past it.
      while (true) {
        if (l >= static_cast<int>(lines.size())) break;
        if (isParagraphSeparatorLine(lines[l])) break;
        int len = static_cast<int>(lines[l].size());
        if (k >= len) {
          gapReachedEof = l == lines.lastLine();
          break;
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
        result.col > static_cast<int>(lines[lastLine].size()) - boundaryOffset) {
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

static bool onlyWhitespaceToLineEnd(const Line& line, int col) {
  for (; col < static_cast<int>(line.size()); col++) {
    if (!CharMask::isWhitespace(line[col])) return false;
  }
  return true;
}

static bool sentenceOperatorEndpointOutsideBoundary(
    CursorPos result, const Lines& lines, bool forward, int boundaryOffset,
    bool hasLinesOutside) {
  int lastLine = lines.lastLine();
  if (forward) {
    if (hasLinesOutside && result.line == lastLine &&
        result.col >= static_cast<int>(lines[lastLine].size())) {
      // Endpoint at NUL of the slice's last line. For non-empty last lines,
      // this is one-past-EOL. For empty last lines (size=0, lastCol=0), col=0
      // is the NUL position. With linesOutside, the real `findsent` would have
      // continued past — the slice-local stop is artificial.
      return true;
    }
    if (hasLinesOutside && result.line == lastLine &&
        result.col < static_cast<int>(lines[lastLine].size()) &&
        onlyWhitespaceToLineEnd(lines[lastLine], result.col)) {
      return true;
    }
    if (result.line > lastLine) {
      return hasLinesOutside || boundaryOffset > 0;
    }
    if (boundaryOffset > 0 && result.line == lastLine &&
        result.col >= static_cast<int>(lines[lastLine].size()) - boundaryOffset) {
      return true;
    }
  } else {
    if (hasLinesOutside && result.line == 0 &&
        result.col <= firstNonBlankColInLine(lines[0])) {
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
  // Port of Neovim's `nv_brace` (normal.c) operator path: run findsent with
  // the raw past-EOL semantics, return the resulting position. If findsent
  // signals FAIL, return cursor unchanged so the interpreter applies no edit
  // (matches Vim's `clearopbeep`).
  if (lines.empty()) return cursor;

  CursorPos result = clampSentencePos(cursor, lines);
  bool ok = findSentenceForOperator(result, lines, forward, 1);
  if (!ok) return cursor;
  return result;
}

CursorPos sentenceOperatorEndpoint(CursorPos cursor, const Lines& lines, bool forward,
                                   int boundaryOffset, bool hasLinesOutside) {
  if (lines.empty()) return cursor;
  CursorPos result = sentenceOperatorEndpointCore(cursor, lines, forward);
  if (sentenceOperatorEndpointOutsideBoundary(
          result, lines, forward, boundaryOffset, hasLinesOutside)) {
    return POSITION_OUTSIDE_BOUNDARY;
  }
  return result;
}

// Helpers ported from Neovim memline.c / textobject.c, used by current_sent
// below. gcharPos returns 0 at NUL/past-end (matching Vim's gchar_pos).
static int gcharPos(const Lines& lines, int line, int col) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return 0;
  const string& ln = lines[line];
  if (col < 0 || col >= static_cast<int>(ln.size())) return 0;
  return static_cast<unsigned char>(ln[col]);
}

static bool isAsciiWhite(int c) { return c == ' ' || c == '\t'; }

// Port of Neovim's find_first_blank (textobject.c:553).
static void findFirstBlank(const Lines& lines, int& line, int& col) {
  while (true) {
    int sl = line, sc = col;
    if (!declPos(lines, sl, sc)) return;
    int c = gcharPos(lines, sl, sc);
    if (!isAsciiWhite(c)) return;  // non-blank: don't update, leave where we were
    line = sl;
    col = sc;
  }
}

// Port of Neovim's findsent_forward (textobject.c:567).
static void findSentForward(const Lines& lines, int& line, int& col,
                            int count, bool atStart) {
  while (count--) {
    CursorPos cp(line, col);
    findSentenceForOperator(cp, lines, true, 1);
    line = cp.line; col = cp.col;
    if (atStart) {
      findFirstBlank(lines, line, col);
    }
    if (count == 0 || atStart) {
      declPos(lines, line, col);
    }
    atStart = !atStart;
  }
}

// Port of Neovim's current_sent (textobject.c:794), non-Visual operator path,
// count == 1. Returns the half-open deletion range matching what Vim's
// op_delete would produce from the (start_pos, cursor, inclusive) triple
// current_sent leaves behind.
//
// The translation to half-open CharRange:
//   - inclusive=false → end = post-incl cursor (exclusive).
//   - inclusive=true (incl failed at buffer end) → end = onePast(cursor's
//     original position), which is NUL position if cursor was on a real char
//     at end of line. op_delete then deletes [start.col, end.col) on the
//     same line, which extends through the line's last char.
CharRange sentenceTextObjectRange(CursorPos cursor, const Lines& lines, bool isInner,
                                  CursorPos leftBoundary, CursorPos rightBoundary) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return CHAR_RANGE_OUTSIDE_BOUNDARY;

  bool include = !isInner;
  int sl = cursor.line, sc = cursor.col;  // start_pos
  int pl = sl, pc = sc;                   // pos (whitespace scan helper)
  int cl = sl, cc = sc;                   // cursor (working)

  // findsent(FORWARD, 1) — move to start of next sentence (or EOF).
  {
    CursorPos cp(cl, cc);
    findSentenceForOperator(cp, lines, true, 1);
    cl = cp.line; cc = cp.col;
  }

  // Scan pos forward through whitespace. If we land where findsent did,
  // the original cursor was inside a whitespace gap before the next sentence.
  while (isAsciiWhite(gcharPos(lines, pl, pc))) {
    if (!inclPos(lines, pl, pc)) break;
  }
  bool startBlank;
  if (pl == cl && pc == cc) {
    startBlank = true;
    findFirstBlank(lines, sl, sc);
  } else {
    startBlank = false;
    CursorPos cp(cl, cc);
    findSentenceForOperator(cp, lines, false, 1);
    cl = cp.line; cc = cp.col;
    sl = cl; sc = cc;
  }

  int ncount = include ? 2 : 1;
  if (!include && startBlank) ncount--;

  if (ncount > 0) {
    findSentForward(lines, cl, cc, ncount, true);
  } else {
    declPos(lines, cl, cc);
  }

  if (include) {
    if (startBlank) {
      findFirstBlank(lines, cl, cc);
      if (isAsciiWhite(gcharPos(lines, cl, cc))) {
        declPos(lines, cl, cc);
      }
    } else if (!isAsciiWhite(gcharPos(lines, cl, cc))) {
      findFirstBlank(lines, sl, sc);
    }
  }

  // current_sent's final block: incl(cursor); if it returns -1, inclusive=true,
  // else inclusive=false. The incl call modifies cursor regardless of return.
  int finalCl = cl, finalCc = cc;
  bool inclSucceeded = inclPos(lines, finalCl, finalCc);
  bool vimInclusive = !inclSucceeded;

  // Vim's nv_object → adjust_cursor_col: if cursor is at NUL of a non-empty
  // line and not in Visual mode, col--. This shifts the cursor back onto a
  // real char, which then lets op_delete compute the right deletion count.
  if (finalCl >= 0 && finalCl < static_cast<int>(lines.size())) {
    int lineSize = static_cast<int>(lines[finalCl].size());
    if (finalCc > 0 && finalCc >= lineSize) {
      finalCc--;
    }
  }

  // Vim's do_pending_operator normalizes (oap->start, curwin->w_cursor):
  // whichever is smaller becomes oap->start, the other becomes oap->end. Then
  // the deletion range is:
  //   inclusive=false → [start, end) half-open (chars [start.col, end.col))
  //   inclusive=true  → [start, end+1) half-open (chars [start.col, end.col])
  CursorPos rangeStart(sl, sc);
  CursorPos rangeEnd(finalCl, finalCc);
  if (rangeStart > rangeEnd) {
    std::swap(rangeStart, rangeEnd);
  }
  CursorPos resultStart = rangeStart;
  CursorPos resultEnd;
  if (vimInclusive) {
    int lineSize = static_cast<int>(lines[rangeEnd.line].size());
    int onePastC = rangeEnd.col < lineSize ? rangeEnd.col + 1 : lineSize;
    resultEnd = CursorPos(rangeEnd.line, onePastC);
  } else {
    resultEnd = rangeEnd;
  }
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
