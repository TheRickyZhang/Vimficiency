#include "VimMotionUtils.h"
#include "VimCore.h"
#include "VimEndpointUtils.h"
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
// Word motions
// =============================================================================
//
// Named motions clamp the shared word scan to Vim cursor landings.
// Edit/text-object ranges use VimEndpointUtils.
//
// =============================================================================

static void motionWord(CursorPos &pos, const Lines &lines,
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
//   w:  Forward  + NextEdge
//   e:  Forward  + WordEdge, skip current first
//   b:  Backward + WordEdge, skip current first
//   ge: Backward + NextEdge, skip current first
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
// Sentence motions
// =============================================================================

namespace {

struct SentenceScanPos {
  int line = 0;
  int col = 0;
};

bool sameSentenceScanPos(SentenceScanPos a, SentenceScanPos b) {
  return a.line == b.line && a.col == b.col;
}

int sentenceCharAt(const Lines& lines, SentenceScanPos pos) {
  if (lines.empty() || pos.line < 0 || pos.line >= static_cast<int>(lines.size())) {
    return -1;
  }
  const string& line = lines[pos.line];
  if (pos.col < 0) return -1;
  if (pos.col >= static_cast<int>(line.size())) return 0;
  return static_cast<unsigned char>(line[pos.col]);
}

int sentenceInc(const Lines& lines, SentenceScanPos& pos) {
  if (lines.empty() || pos.line < 0 || pos.line >= static_cast<int>(lines.size())) {
    return -1;
  }
  const int len = static_cast<int>(lines[pos.line].size());
  if (pos.col < len) {
    pos.col++;
    return sentenceCharAt(lines, pos) == 0 ? 2 : 0;
  }
  if (pos.line + 1 >= static_cast<int>(lines.size())) {
    return -1;
  }
  pos.line++;
  pos.col = 0;
  return 1;
}

int sentenceIncl(const Lines& lines, SentenceScanPos& pos) {
  int result = sentenceInc(lines, pos);
  if (result >= 1 && pos.col != 0) {
    result = sentenceInc(lines, pos);
  }
  return result;
}

int sentenceDecl(const Lines& lines, SentenceScanPos& pos) {
  if (lines.empty() || pos.line < 0 || pos.line >= static_cast<int>(lines.size())) {
    return -1;
  }
  if (pos.col > 0) {
    pos.col--;
    return sentenceCharAt(lines, pos);
  }
  if (pos.line == 0) {
    return -1;
  }
  pos.line--;
  pos.col = max(0, static_cast<int>(lines[pos.line].size()) - 1);
  return sentenceCharAt(lines, pos);
}

bool sentenceIsWhite(int c) {
  return c == ' ' || c == '\t';
}

bool sentenceIsEndPunct(int c) {
  return c == '.' || c == '!' || c == '?';
}

bool sentenceIsPunctOrCloser(int c) {
  return sentenceIsEndPunct(c) || c == ')' || c == ']' ||
         c == '"' || c == '\'';
}

bool sentenceInMacro(string_view opt, string_view s) {
  for (size_t i = 0; i < opt.size(); i += 2) {
    char first = opt[i];
    char second = (i + 1 < opt.size()) ? opt[i + 1] : '\0';
    char s0 = s.empty() ? '\0' : s[0];
    char s1 = s.size() < 2 ? '\0' : s[1];

    bool firstMatches = first == s0 || (first == ' ' && (s0 == '\0' || s0 == ' '));
    bool secondMatches = second == s1 ||
        ((second == '\0' || second == ' ') && (s0 == '\0' || s1 == '\0' || s1 == ' '));
    if (firstMatches && secondMatches) return true;
  }
  return false;
}

bool sentenceStartPS(const Lines& lines, int line, int para = 0, bool both = false) {
  if (line < 0 || line >= static_cast<int>(lines.size())) return false;
  const string& s = lines[line];
  char first = s.empty() ? '\0' : s[0];
  if (static_cast<unsigned char>(first) == para || first == '\f' ||
      (both && first == '}')) {
    return true;
  }
  static constexpr string_view kSections = "SHNHH HUnhsh";
  static constexpr string_view kParagraphs = "IPLPPPQPP TPHPLIPpLpItpplpipbp";
  return first == '.' &&
      (sentenceInMacro(kSections, string_view(s).substr(1)) ||
       (para == 0 && sentenceInMacro(kParagraphs, string_view(s).substr(1))));
}

CursorPos sentenceScanToCursor(const Lines& lines, SentenceScanPos pos) {
  if (lines.empty()) return {0, 0};
  pos.line = clamp(pos.line, 0, static_cast<int>(lines.size()) - 1);
  int len = static_cast<int>(lines[pos.line].size());
  if (len == 0) return {pos.line, 0};
  return {pos.line, clamp(pos.col, 0, len - 1)};
}

bool findSentenceLikeNeovim(CursorPos& cursor, const Lines& lines, bool forward, int count) {
  if (lines.empty() || count <= 0) return false;

  SentenceScanPos pos{
      clamp(cursor.line, 0, static_cast<int>(lines.size()) - 1),
      cursor.col,
  };
  int len = static_cast<int>(lines[pos.line].size());
  pos.col = len == 0 ? 0 : clamp(pos.col, 0, len - 1);

  bool noskip = false;
  while (count > 0) {
    count--;
    SentenceScanPos prevPos = pos;
    int c = sentenceCharAt(lines, pos);

    if (c == 0) {
      do {
        if ((forward ? sentenceIncl(lines, pos) : sentenceDecl(lines, pos)) == -1) {
          break;
        }
      } while (sentenceCharAt(lines, pos) == 0);
      if (forward) {
        goto found;
      }
    } else if (forward && pos.col == 0 && sentenceStartPS(lines, pos.line)) {
      if (pos.line == static_cast<int>(lines.size()) - 1) {
        return false;
      }
      pos.line++;
      pos.col = 0;
      goto found;
    } else if (!forward) {
      sentenceDecl(lines, pos);
    }

    {
      bool foundDot = false;
      while (true) {
        c = sentenceCharAt(lines, pos);
        if (!sentenceIsWhite(c) && !sentenceIsPunctOrCloser(c)) {
          break;
        }

        SentenceScanPos tpos = pos;
        if (sentenceDecl(lines, tpos) == -1 ||
            (lines[tpos.line].empty() && forward)) {
          break;
        }
        if (foundDot) {
          break;
        }
        if (sentenceIsEndPunct(c)) {
          foundDot = true;
        }
        if ((c == ')' || c == ']' || c == '"' || c == '\'') &&
            !sentenceIsPunctOrCloser(sentenceCharAt(lines, tpos))) {
          break;
        }
        sentenceDecl(lines, pos);
      }
    }

    {
      int startLine = pos.line;
      while (true) {
        c = sentenceCharAt(lines, pos);
        if (c == 0 || (pos.col == 0 && sentenceStartPS(lines, pos.line))) {
          if (!forward && pos.line != startLine) {
            pos.line++;
            pos.col = 0;
          }
          break;
        }

        if (sentenceIsEndPunct(c)) {
          SentenceScanPos tpos = pos;
          int next = 0;
          do {
            next = sentenceInc(lines, tpos);
            if (next == -1) break;
          } while (sentenceCharAt(lines, tpos) == ')' ||
                   sentenceCharAt(lines, tpos) == ']' ||
                   sentenceCharAt(lines, tpos) == '"' ||
                   sentenceCharAt(lines, tpos) == '\'');

          int after = sentenceCharAt(lines, tpos);
          if (next == -1 || sentenceIsWhite(after) || after == 0) {
            pos = tpos;
            if (sentenceCharAt(lines, pos) == 0) {
              sentenceIncl(lines, pos);
            }
            break;
          }
        }

        if ((forward ? sentenceIncl(lines, pos) : sentenceDecl(lines, pos)) == -1) {
          if (count > 0) {
            return false;
          }
          noskip = true;
          break;
        }
      }
    }

found:
    while (!noskip && sentenceIsWhite(sentenceCharAt(lines, pos))) {
      if (sentenceIncl(lines, pos) == -1) {
        break;
      }
    }

    if (sameSentenceScanPos(prevPos, pos)) {
      if ((forward ? sentenceIncl(lines, pos) : sentenceDecl(lines, pos)) == -1) {
        if (count > 0) {
          return false;
        }
        break;
      }
      count++;
    }
  }

  cursor = sentenceScanToCursor(lines, pos);
  return true;
}

}  // namespace

void motionSentenceNext(CursorPos &pos, const Lines &lines) {
  findSentenceLikeNeovim(pos, lines, true, 1);
}

void motionSentencePrev(CursorPos &pos, const Lines &lines) {
  findSentenceLikeNeovim(pos, lines, false, 1);
}

// =============================================================================
// Character Find (f/F/t/T)
// =============================================================================

// Returns destination column, or -1 if target not found
// forward: true for f/t, false for F/T
// till: true for t/T (stop one short), false for f/F (land on target)
int findCharInLine(char target, string_view line,
                   int startCol, bool forward, bool till) {
  return findCharInLine(target, line, startCol, forward, till,
                        /*count=*/1, /*repeat=*/false);
}

int findCharInLine(char target, string_view line, int startCol,
                   bool forward, bool till, int count, bool repeat) {
  if (count <= 0) return -1;

  const int n = static_cast<int>(line.size());
  const int step = forward ? 1 : -1;
  int i = startCol + step;

  // Repeating t/T skips the adjacent target the prior till stopped beside.
  if (repeat && till) {
    i += step;
  }

  int seen = 0;
  for (; i >= 0 && i < n; i += step) {
    if (line[i] == target) {
      seen++;
      if (seen == count) {
        return till ? i - step : i;
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
