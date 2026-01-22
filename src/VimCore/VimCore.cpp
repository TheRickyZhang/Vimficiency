#include "VimCore.h"

#include <algorithm>
#include <cassert>

using namespace std;

namespace VimCore {

// =============================================================================
// 1. Character Classification
// =============================================================================

bool isWhitespace(unsigned char c) {
  return c == ' ' || c == '\t';
}

bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

bool isSmallWordChar(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

bool isBigWordChar(unsigned char c) {
  return c != 0 && !isBlank(c) && c != '\n';
}

bool isSentenceEnd(unsigned char c) {
  return c == '.' || c == '!' || c == '?';
}

bool isSentenceCloser(unsigned char c) {
  return c == ')' || c == ']' || c == '"' || c == '\'';
}

// =============================================================================
// 2. String/Line Helpers
// =============================================================================

bool isBlankLineStr(const std::string& s) {
  for (unsigned char c : s) {
    if (c != ' ' && c != '\t')
      return false;
  }
  return true; // empty or whitespace-only
}

int firstNonBlankColInLineStr(const std::string& s) {
  for (int i = 0; i < (int)s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c != ' ' && c != '\t')
      return i;
  }
  return 0;
}

// =============================================================================
// 3. Position Stepping (old int-based API)
// =============================================================================

// Char at (line,col):
// - Returns 0 only if line is out of range.
// - Returns '\n' if col is outside the actual line (used as "newline/blank"
//   sentinel for word motions).
unsigned char getChar(const Lines& lines, int line, int col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return 0;
  const auto& ln = lines[line];
  if (col < 0 || col >= static_cast<int>(ln.size()))
    return '\n';
  return static_cast<unsigned char>(ln[col]);
}

// Step forward one character in the logical buffer (across lines).
// Returns false if already at (or past) the last character.
bool stepFwd(const Lines& lines, int& line, int& col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return false;
  const auto& ln = lines[line];
  int len = static_cast<int>(ln.size());
  if (col + 1 < len) {
    ++col;
    return true;
  }
  if (line + 1 < n) {
    ++line;
    col = 0;
    return true;
  }
  return false;
}

// Step backward one character in the logical buffer (across lines).
// Returns false if already at (or before) the first character.
bool stepBack(const Lines& lines, int& line, int& col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return false;
  if (col > 0) {
    --col;
    return true;
  }
  if (line > 0) {
    --line;
    col = static_cast<int>(lines[line].size());
    if (col > 0)
      --col; // last real character of previous line (if any)
    return true;
  }
  return false;
}

// =============================================================================
// 4. Word Motion Core
// =============================================================================

// Returns: Position after perfoaming - POSITION_OUTSIDE_BOUNDARY: motion hit buffer edge (for boundary detection)
Position motionWordCore(Position pos,
                        const Lines& lines,
                        bool forward,
                        EdgeType edge,
                        bool big,
                        bool skipCurrent) {
  // ---------------------------------------------------------------------------
  // skipCurrent: Move off current position before searching
  // ---------------------------------------------------------------------------
  // Used by: e/E, b/B, ge/gE. For instance, 'e' at end of word should find NEXT word's end
  //
  // Some motions terminate early after just this one step:
  //   - b/B landing on empty line: empty line IS a word (vim: "An empty line is also a word")
  //   - ge/gE crossing word boundary: line boundaries count as word boundaries for ge/gE (unlike 'e' which "does not stop in an empty line")
  if (skipCurrent) {
    Position prevPos = pos;
    unsigned char prevChar = lines.get(pos);

    pos = step(lines, pos, forward);
    if (pos == prevPos) {
      return POSITION_OUTSIDE_BOUNDARY;  // At buffer edge
    }

    unsigned char currChar = lines.get(pos);

    // b/B (backward WordEdge): Empty line is a word, stop here
    if (!forward && edge == EdgeType::WordEdge && currChar == '\n') {
      return pos;
    }

    // ge/gE (backward NextEdge): Line/word boundaries are stopping points
    // If the single step crossed a word boundary and landed on non-blank, done.
    if (!forward && edge == EdgeType::NextEdge) {
      bool crossedLine = (pos.line != prevPos.line);
      bool crossedWordBoundary =
          crossedLine || isBlank(currChar) || isBlank(prevChar) ||
          (!big && isSmallWordChar(currChar) != isSmallWordChar(prevChar));
      if (crossedWordBoundary && !isBlank(currChar)) {
        return pos;
      }
    }
    // e/E (forward WordEdge): No early termination - "does not stop in an empty line"
    // Note: w/W (forward NextEdge) uses skipCurrent=false, so not handled here.
  }

  unsigned char c = lines.get(pos);

  // Phase 1: If starting on blank, skip to first non-blank
  if (isBlank(c)) {
    do {
      Position prev = pos;
      pos = step(lines, pos, forward);
      if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;
      c = lines.get(pos);
    } while (isBlank(c));

    if (edge == EdgeType::NextEdge) return pos;
    if (edge == EdgeType::GapEdge) return stepBack(lines, pos, forward);
  }

  // Phase 2: Skip same-type chars (current word)
  bool startIsWordChar = isSmallWordChar(c);
  bool crossedLine = false;
  do {
    Position prev = pos;
    pos = step(lines, pos, forward);
    if (pos == prev) {
      // Hit buffer edge - for WordEdge, return prev (last char of word)
      if (edge == EdgeType::WordEdge) return prev;
      return POSITION_OUTSIDE_BOUNDARY;
    }
    c = lines.get(pos);
    if (pos.line != prev.line) {
      crossedLine = true;
      break;
    }
  } while (!isBlank(c) && (big || isSmallWordChar(c) == startIsWordChar));

  if (edge == EdgeType::WordEdge) return stepBack(lines, pos, forward);

  // Empty line is a word
  if (crossedLine && c == '\n') {
    if (edge == EdgeType::NextEdge) return pos;
    if (edge == EdgeType::GapEdge) return stepBack(lines, pos, forward);
  }

  // Phase 3: If at non-blank different type (word only, not WORD)
  if (!isBlank(c)) {
    if (edge == EdgeType::NextEdge) return pos;
    if (edge == EdgeType::GapEdge) return stepBack(lines, pos, forward);
  }

  // Phase 4: Skip whitespace to reach next word (but stop at empty lines)
  while (isWhitespace(c)) {
    Position prev = pos;
    pos = step(lines, pos, forward);
    if (pos == prev) return POSITION_OUTSIDE_BOUNDARY;
    c = lines.get(pos);
    if (c == '\n') {
      if (edge == EdgeType::NextEdge) return pos;
      if (edge == EdgeType::GapEdge) return stepBack(lines, pos, forward);
    }
  }

  if (edge == EdgeType::GapEdge) return stepBack(lines, pos, forward);
  return pos;
}

// =============================================================================
// 5. Paragraph Helpers
// =============================================================================

// Returns the first line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line run.
int paragraphStartLine(const Lines& lines, int lineIdx) {
  int n = (int)lines.size();
  if (n == 0)
    return 0;
  lineIdx = std::clamp(lineIdx, 0, n - 1);

  bool blank = isBlankLineStr(lines[lineIdx]);
  int i = lineIdx;
  while (i > 0 && isBlankLineStr(lines[i - 1]) == blank)
    --i;
  return i;
}

// Returns the last line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line run.
int paragraphEndLine(const Lines& lines, int lineIdx) {
  int n = (int)lines.size();
  if (n == 0)
    return 0;
  lineIdx = std::clamp(lineIdx, 0, n - 1);

  bool blank = isBlankLineStr(lines[lineIdx]);
  int i = lineIdx;
  while (i + 1 < n && isBlankLineStr(lines[i + 1]) == blank)
    ++i;
  return i;
}

// =============================================================================
// 6. Sentence Helpers
// =============================================================================

// Sentence end at (line,col): . ! ?  then optional closers  then (EOL or space/tab)
bool isSentenceEndAt(const Lines& lines, int line, int col) {
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

std::pair<int, int>
skipToSentenceStart(const Lines& lines, int line, int col) {
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

std::pair<int, int>
findCurrentSentenceStart(const Lines& lines, int line, int col) {
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

} // namespace VimCore
