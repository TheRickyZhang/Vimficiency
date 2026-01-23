#include "VimMotionUtils.h"
#include "VimCore.h"
#include "VimEndpointUtils.h"
#include "EdgeType.h"
#include "LineEdgeType.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "Editor/Position.h"
#include "Utils/Debug.h"

using namespace std;

namespace VimCore {

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

void motionWord(Position &pos, const Lines &lines,
                bool forward, EdgeType edgeType, bool big,
                bool skipCurrent) {
  Position result =
      motionWordCore(pos, lines, forward, edgeType, big, skipCurrent);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    // Vim clamps to buffer edge based on direction
    if (forward) {
      int lastLine = lines.lastLine();
      int lastCol = lines[lastLine].empty()
                        ? 0
                        : static_cast<int>(lines[lastLine].size()) - 1;
      pos = Position(lastLine, lastCol);
    } else {
      pos = Position(0, 0);
    }
  } else {
    pos = result;
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

void motionW(Position &pos, const Lines &lines, bool big) {
  motionWord(pos, lines, true, EdgeType::NextEdge, big, false);
}

void motionB(Position &pos, const Lines &lines, bool big) {
  // For backward direction, End gives edge opposite to travel = leftmost =
  // START skipCurrent needed so b from word start goes to PREVIOUS word start
  motionWord(pos, lines, false, EdgeType::WordEdge, big, true);
}

void motionE(Position &pos, const Lines &lines, bool big) {
  // e needs to skip current position first, otherwise we'd stay at current word
  // end
  motionWord(pos, lines, true, EdgeType::WordEdge, big, true);
}

void motionGe(Position &pos, const Lines &lines, bool big) {
  // For backward direction, Next gives edge in travel direction = rightmost =
  // END skipCurrent needed so ge from word end goes to PREVIOUS word end
  motionWord(pos, lines, false, EdgeType::NextEdge, big, true);
}

// =============================================================================
// Paragraph motion forwarders
// =============================================================================

void motionParagraphPrev(Position &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  pos.line = motionParagraphEndpoint(pos.line, lines, false,
                                                       LineEdgeType::NextEdge);
  pos.setCol(0);
}

void motionParagraphNext(Position &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  int resultLine = motionParagraphEndpoint(
      pos.line, lines, true, LineEdgeType::NextEdge);
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
// Position helpers
// =============================================================================

int clampCol(const Lines &lines, int col, int lineIdx) {
  int n = static_cast<int>(lines.size());
  assert(lineIdx >= 0 && lineIdx < n);
  int len = static_cast<int>(lines[lineIdx].size());
  if (len == 0)
    return 0;
  return std::clamp(col, 0, len - 1);
}

void moveCol(Position &pos, const Lines &lines, int dx) {
  pos.setCol(clampCol(lines, pos.col + dx, pos.line));
}

void moveLine(Position &pos, const Lines &lines, int dy) {
  int n = static_cast<int>(lines.size());
  pos.line = std::clamp(pos.line + dy, 0, n - 1);
  // Vertical movement: clamp col to line length but preserve targetCol
  pos.clampColPreservingTarget(clampCol(lines, pos.targetCol, pos.line));
}

// =============================================================================
// Paragraph edge helpers
// =============================================================================

// Move to the "top edge" (start) of the current paragraph.
// If currently on blank lines, goes to first blank line in that run.
void moveToParagraphStart(Position &pos, const Lines &lines) {
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
void moveToParagraphEnd(Position &pos, const Lines &lines) {
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

void motionSentenceNext(Position &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  int line = std::clamp(pos.line, 0, n - 1);
  int col = (int)lines[line].size() == 0
                ? 0
                : std::clamp(pos.col, 0, (int)lines[line].size() - 1);

  // If currently on blank run: jump to next nonblank line start.
  // Special case: if all remaining lines are blank, move to next blank line (if
  // any).
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
            // We're in the gap after a sentence end - skip to next sentence
            // start
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

void motionSentencePrev(Position &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

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
      // Skip whitespace and closers
      if (c == ' ' || c == '\t' || isSentenceCloser(c)) {
        continue;
      }

      // Sentence-ending punctuation could be a single-char sentence start
      // if it's preceded by whitespace (after a previous sentence end).
      // Example: ". . c" - the second '.' is a sentence start.
      // But ".." is NOT two sentences - only the last '.' is a sentence end.
      if (c == '.' || c == '!' || c == '?') {
        // Check the character before this punctuation
        int prevL = l, prevK = k;
        if (stepBack(lines, prevL, prevK)) {
          unsigned char prevC = getChar(lines, prevL, prevK);
          // Only if preceded by whitespace is this punctuation a sentence start.
          // Preceded by another punctuation (like "..") means it's part of a
          // multi-punctuation sequence, not a standalone sentence.
          if (prevC == ' ' || prevC == '\t' || prevC == '\n') {
            // This punctuation is the start of a single-char sentence
            pos.line = l;
            pos.setCol(k);
            return;
          }
        } else {
          // At buffer start - this is the sentence start
          pos.line = l;
          pos.setCol(k);
          return;
        }
        // Not preceded by whitespace - skip this punctuation
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

// =============================================================================
// Character Find (f/F/t/T)
// =============================================================================

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int findCharInLine(char target, const string &line,
                   int startCol, bool forward, bool till) {
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

// =============================================================================
// Template instantiations
// =============================================================================

// Return char since f motions are guaranteed to just be one character. Will be
// converted to string further up.
template <bool Forward>
vector<tuple<char, int, int>>
generateFMotions(int currCol, int targetCol,
                 const string &line, int threshold) {
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

template std::vector<std::tuple<char, int, int>>
generateFMotions<true>(int, int, const std::string &, int);

template std::vector<std::tuple<char, int, int>>
generateFMotions<false>(int, int, const std::string &, int);

} // namespace VimCore
