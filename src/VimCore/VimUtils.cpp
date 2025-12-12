#include "VimUtils.h"

#include <algorithm>
#include <cassert>
#include <cctype>

#include "../State.h"

// -----------------------------------------------------------------------------
// Low-level helpers
// -----------------------------------------------------------------------------

int VimUtils::clampCol(const std::vector<std::string> &lines,
                       int col,
                       int lineIdx) {
  int n = static_cast<int>(lines.size());
  assert(lineIdx >= 0 && lineIdx < n);
  int len = static_cast<int>(lines[lineIdx].size());
  if(len == 0) return 0;
  return std::clamp(col, 0, len - 1);
}

void VimUtils::moveCol(Position &pos,
                       const std::vector<std::string> &lines,
                       int dx) {
  pos.setCol(clampCol(lines, pos.col + dx, pos.line));
}

void VimUtils::moveLine(Position &pos,
                        const std::vector<std::string> &lines,
                        int dy) {
  int n = static_cast<int>(lines.size());
  pos.line = std::clamp(pos.line + dy, 0, n - 1);
  pos.setCol(clampCol(lines, pos.col, pos.line));
}

// Char at (line,col):
// - Returns 0 only if line is out of range.
// - Returns '\n' if col is outside the actual line (used as "newline/blank"
//   sentinel for word motions).
unsigned char VimUtils::getChar(const std::vector<std::string> &lines,
                                int line,
                                int col) {
  int n = static_cast<int>(lines.size());
  if(line < 0 || line >= n) return 0;
  const auto &ln = lines[line];
  if(col < 0 || col >= static_cast<int>(ln.size())) return '\n';
  return static_cast<unsigned char>(ln[col]);
}

// Step forward one character in the logical buffer (across lines).
// Returns false if already at (or past) the last character.
bool VimUtils::stepFwd(const std::vector<std::string> &lines,
                       int &line,
                       int &col) {
  int n = static_cast<int>(lines.size());
  if(line < 0 || line >= n) return false;
  const auto &ln = lines[line];
  int len = static_cast<int>(ln.size());

  // Within line
  if(col + 1 < len) {
    ++col;
    return true;
  }

  // Past end of line → next line, first column
  if(line + 1 < n) {
    ++line;
    col = 0;
    return true;
  }

  // EOF
  return false;
}

// Step backward one character in the logical buffer (across lines).
// Returns false if already at (or before) the first character.
bool VimUtils::stepBack(const std::vector<std::string> &lines,
                        int &line,
                        int &col) {
  int n = static_cast<int>(lines.size());
  if(line < 0 || line >= n) return false;

  if(col > 0) {
    --col;
    return true;
  }

  if(line > 0) {
    --line;
    col = static_cast<int>(lines[line].size());
    if(col > 0) --col; // last real character of previous line (if any)
    return true;
  }

  return false;
}

// -----------------------------------------------------------------------------
// Classification helpers (mirror Vim docs semantics)
// -----------------------------------------------------------------------------
//
// Small "word":
//   sequence of [A-Za-z0-9_] (approximating 'iskeyword' default) :contentReference[oaicite:1]{index=1}
// Big "WORD":
//   sequence of any non-blank (non-space, non-tab, non-newline) chars.
//
// Newline ('\n') is treated as blank for motions.
// -----------------------------------------------------------------------------

bool VimUtils::isBlank(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

bool VimUtils::isSmallWordChar(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

bool VimUtils::isBigWordChar(unsigned char c) {
  return c != 0 && !isBlank(c) && c != '\n';
}

// -----------------------------------------------------------------------------
// Word motions: w, b, e / W, B, E
//
// Goal: approximate Vim’s word semantics.
//
//  - w/W: move to start of next "word"/"WORD" :contentReference[oaicite:2]{index=2}
//  - b/B: move to start of current word if inside it; otherwise previous.
//  - e/E: move to end of current word if not already at end; else next.
// -----------------------------------------------------------------------------

void VimUtils::motion_w(Position &pos,
                        const std::vector<std::string> &lines,
                        bool big) {
  int line = pos.line;
  int col  = pos.col;

  auto isWord = [big](unsigned char c) {
    return big ? isBigWordChar(c) : isSmallWordChar(c);
  };

  unsigned char c0 = getChar(lines, line, col);
  if(c0 == 0) {
    // Out of buffer
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Case 1: starting on blank (space/tab/newline) → skip blanks to next
  // non-blank.
  if(isBlank(c0)) {
    while(true) {
      if(!stepFwd(lines, line, col)) {
        pos.line = line;
        pos.col  = col;
        return;
      }
      unsigned char c = getChar(lines, line, col);
      if(!isBlank(c)) break;
    }
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Case 2: starting on non-blank (either "keyword word" or "symbol word").
  bool inWord = isWord(c0);

  // Skip the *current* word/anti-word group.
  while(true) {
    int oldLine = line;
    int oldCol  = col;

    if(!stepFwd(lines, line, col)) {
      // Reached EOF; stay there.
      pos.line = line;
      pos.col  = col;
      return;
    }

    unsigned char c = getChar(lines, line, col);

    // Line wrap acts like hitting a newline: we consider that a boundary.
    bool wrapped = (line != oldLine);
    if(wrapped) {
      break;
    }

    // Boundary: whitespace or transition between word/non-word groups.
    if(isBlank(c)) break;
    if(isWord(c) != inWord) break;
  }

  // Now at the first character *after* the skipped group, which might be:
  //  - start of next group (word or "symbol word")
  //  - whitespace
  unsigned char c = getChar(lines, line, col);
  if(!isBlank(c)) {
    // Start of next (word|anti-word) group: this is where Vim's 'w' lands
    // when there is no intermediate whitespace.
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Otherwise, we're on whitespace → skip blanks to first non-blank.
  while(true) {
    if(!stepFwd(lines, line, col)) {
      pos.line = line;
      pos.col  = col;
      return;
    }
    c = getChar(lines, line, col);
    if(!isBlank(c)) break;
  }

  pos.line = line;
  pos.col  = col;
}

void VimUtils::motion_b(Position &pos,
                        const std::vector<std::string> &lines,
                        bool big) {
  int line = pos.line;
  int col  = pos.col;

  auto isWord = [big](unsigned char c) {
    return big ? isBigWordChar(c) : isSmallWordChar(c);
  };

  unsigned char c = getChar(lines, line, col);
  if(c == 0) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  // Step 1: if on blank, move backward over blanks.
  if(isBlank(c)) {
    while(true) {
      if(!stepBack(lines, line, col)) {
        pos.line = line;
        pos.col  = col;
        return;
      }
      c = getChar(lines, line, col);
      if(!isBlank(c)) break;
    }
  }

  // Now we are on a non-blank character (word or symbol group).
  c = getChar(lines, line, col);
  if(c == 0) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  bool inWord = isWord(c);

  // Step 2: move left while previous character is in the same group and
  // non-blank, so we end at the *first* char of that word/anti-word.
  while(true) {
    int prevLine = line;
    int prevCol  = col;
    if(!stepBack(lines, prevLine, prevCol)) break;

    unsigned char pc = getChar(lines, prevLine, prevCol);
    if(isBlank(pc)) break;
    if(isWord(pc) != inWord) break;

    line = prevLine;
    col  = prevCol;
  }

  pos.line = line;
  pos.col  = col;
}

void VimUtils::motion_e(Position &pos,
                        const std::vector<std::string> &lines,
                        bool big) {
  int line = pos.line;
  int col  = pos.col;

  auto isWord = [big](unsigned char c) {
    return big ? isBigWordChar(c) : isSmallWordChar(c);
  };

  // Step 1: move forward one character.
  // This aligns with Vim's "current-or-next word end" semantics. :contentReference[oaicite:3]{index=3}
  if(!stepFwd(lines, line, col)) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  unsigned char c = getChar(lines, line, col);

  // Step 2: skip blanks (spaces, tabs, logical newlines/empty lines).
  while(isBlank(c)) {
    if(!stepFwd(lines, line, col)) {
      pos.line = line;
      pos.col  = col;
      return;
    }
    c = getChar(lines, line, col);
  }

  if(c == 0) {
    pos.line = line;
    pos.col  = col;
    return;
  }

  bool inWord = isWord(c);

  // Step 3: now at first char of the target word/anti-word.
  // Advance until the last character of that group.
  while(true) {
    int nextLine = line;
    int nextCol  = col;
    if(!stepFwd(lines, nextLine, nextCol)) {
      // EOF → current position is already last char.
      break;
    }

    unsigned char nc = getChar(lines, nextLine, nextCol);
    if(isBlank(nc)) break;
    if(isWord(nc) != inWord) break;

    line = nextLine;
    col  = nextCol;
  }

  pos.line = line;
  pos.col  = col;
}
