#include "VimMotionUtils.h"
#include "VimCore.h"
#include "VimEndpointUtils.h"
#include "VimPortedImpl.h"
#include "Types/EdgeType.h"
#include "Types/LineEdgeType.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "Types/CursorPos.h"
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

void motionWord(CursorPos &pos, const Lines &lines,
                bool forward, EdgeType edgeType, bool big,
                bool skipCurrent) {
  CursorPos result =
      motionWordCore(pos, lines, forward, edgeType, big, skipCurrent);

  if (result == POSITION_OUTSIDE_BOUNDARY) {
    // Vim clamps to buffer edge based on direction
    if (forward) {
      int lastLine = lines.lastLine();
      int lastCol = lines[lastLine].empty()
                        ? 0
                        : static_cast<int>(lines[lastLine].size()) - 1;
      pos = CursorPos(lastLine, lastCol);
    } else {
      pos = CursorPos(0, 0);
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

void motionW(CursorPos &pos, const Lines &lines, bool big) {
  motionWord(pos, lines, true, EdgeType::NextEdge, big, false);
}

void motionB(CursorPos &pos, const Lines &lines, bool big) {
  // For backward direction, End gives edge opposite to travel = leftmost =
  // START skipCurrent needed so b from word start goes to PREVIOUS word start
  motionWord(pos, lines, false, EdgeType::WordEdge, big, true);
}

void motionE(CursorPos &pos, const Lines &lines, bool big) {
  // e needs to skip current position first, otherwise we'd stay at current word
  // end
  motionWord(pos, lines, true, EdgeType::WordEdge, big, true);
}

void motionGe(CursorPos &pos, const Lines &lines, bool big) {
  // For backward direction, Next gives edge in travel direction = rightmost =
  // END skipCurrent needed so ge from word end goes to PREVIOUS word end
  motionWord(pos, lines, false, EdgeType::NextEdge, big, true);
}

// =============================================================================
// Paragraph motion forwarders
// =============================================================================

void motionParagraphPrev(CursorPos &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;

  pos.line = motionParagraphEndpoint(pos.line, lines, false,
                                                       LineEdgeType::NextEdge);
  pos.setCol(0);
}

void motionParagraphNext(CursorPos &pos, const Lines &lines) {
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
// CursorPos helpers
// =============================================================================

int clampCol(const Lines &lines, int col, int lineIdx) {
  int n = static_cast<int>(lines.size());
  assert(lineIdx >= 0 && lineIdx < n);
  int len = static_cast<int>(lines[lineIdx].size());
  if (len == 0)
    return 0;
  return std::clamp(col, 0, len - 1);
}

void moveCol(CursorPos &pos, const Lines &lines, int dx) {
  pos.setCol(clampCol(lines, pos.col + dx, pos.line));
}

void moveLine(CursorPos &pos, const Lines &lines, int dy) {
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
void moveToParagraphStart(CursorPos &pos, const Lines &lines) {
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
void moveToParagraphEnd(CursorPos &pos, const Lines &lines) {
  int n = (int)lines.size();
  if (n == 0)
    return;
  pos.line = std::clamp(pos.line, 0, n - 1);
  pos.line = paragraphEndLine(lines, pos.line);

  // Keep column in bounds.
  pos.setCol(clampCol(lines, pos.col, pos.line));
}

// =============================================================================
// Sentence motions — closely follows Neovim's findsent() algorithm
// =============================================================================
//
// Uses VimCore's Pos-based stepping (section 3b) which models Vim's virtual
// NUL terminator at col == lineLen. vimInc/vimDec can land on NUL;
// vimIncl/vimDecl skip it. See VimCore.h for details.

namespace {

// Convert Vim-model Pos to valid CursorPos (clamp NUL col to last real char).
inline void posToCursor(const Lines& lines, Pos p, CursorPos& out) {
  int n = (int)lines.size();
  int l = std::clamp(p.line, 0, n - 1);
  int len = (int)lines[l].size();
  out.line = l;
  out.setCol(len == 0 ? 0 : std::clamp(p.col, 0, len - 1));
}

// Core sentence motion matching Neovim's findsent().
static void findsent(CursorPos& pos, const Lines& lines, bool forward) {
  int n = (int)lines.size();
  if (n == 0) return;

  int startLine = std::clamp(pos.line, 0, n - 1);
  int startCol = lines[startLine].empty() ? 0
      : std::clamp(pos.col, 0, (int)lines[startLine].size() - 1);
  Pos cur(startLine, startCol);
  Pos prev = cur;
  bool noskip = false;
  bool foundTarget = false;

  // --- Step 1: Handle empty line / paragraph boundary / backward decrement ---
  unsigned char c = vimGchar(lines, cur);
  if (c == 0) {
    // On empty line — skip empty lines in motion direction
    do {
      Pos next = forward ? vimIncl(lines, cur) : vimDecl(lines, cur);
      if (!next.isValid()) break;
      cur = next;
    } while (vimGchar(lines, cur) == 0);
    if (forward) foundTarget = true;
  } else if (forward && cur.col == 0 && isParaBoundaryLine(lines, cur.line)) {
    if (cur.line + 1 >= n) return;
    cur = Pos(cur.line + 1, 0);
    foundTarget = true;
  } else if (!forward) {
    Pos next = vimDecl(lines, cur);
    if (next.isValid()) cur = next;
  }

  if (!foundTarget) {
    // --- Step 2: Back up past whitespace and sentence punctuation ---
    // Uses decl (not dec) so it crosses line boundaries, skipping NUL.
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

        // Closers: only back up if preceded by another sentence char
        if (isSentenceCloser(c)) {
          unsigned char tc = vimGchar(lines, probe);
          if (!isSentenceEnd(tc) && !isSentenceCloser(tc) && !isWhitespace(tc))
            break;
        }

        cur = vimDecl(lines, cur);
      }
    }

    // --- Step 3: Find end of sentence ---
    // Forward uses incl (skips NUL, crosses lines). Backward uses decl.
    // The .!? check uses inc (not incl) so it can see NUL at EOL.
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

  // --- Step 4: Skip whitespace (always forward, using incl) ---
  if (!noskip) {
    while (true) {
      c = vimGchar(lines, cur);
      if (!isWhitespace(c)) break;
      Pos next = vimIncl(lines, cur);
      if (!next.isValid()) break;
      cur = next;
    }
  }

  // --- Step 5: Retry if position unchanged ---
  if (cur == prev) {
    Pos next = forward ? vimIncl(lines, cur) : vimDecl(lines, cur);
    if (!next.isValid()) {
      posToCursor(lines, cur, pos);
      return;
    }
    cur = next;
    posToCursor(lines, cur, pos);
    findsent(pos, lines, forward);
    return;
  }

  posToCursor(lines, cur, pos);
}

} // anonymous namespace

void motionSentenceNext(CursorPos &pos, const Lines &lines) {
  pos = VimCore::findsent(pos, lines, true);
}

void motionSentencePrev(CursorPos &pos, const Lines &lines) {
  pos = VimCore::findsent(pos, lines, false);
}

// =============================================================================
// Character Find (f/F/t/T)
// =============================================================================

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int findCharInLine(char target, string_view line,
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
                 string_view line, int threshold) {
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
generateFMotions<true>(int, int, std::string_view, int);

template std::vector<std::tuple<char, int, int>>
generateFMotions<false>(int, int, std::string_view, int);

} // namespace VimCore
