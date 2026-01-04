#include "VimUtils.h"

#include <algorithm>
#include <cassert>
#include <cctype>

using namespace std;

// -----------------------------------------------------------------------------
// Classification helpers (mirror Vim semantics)
// -----------------------------------------------------------------------------

// Small "word": sequence of [A-Za-z0-9_] (approximating 'iskeyword')
// Big "WORD": sequence of any non-blank (non-space, non-tab, non-newline)
// chars.
//
// Newline ('\n') is treated as blank for motions.
// -----------------------------------------------------------------------------

namespace VimUtils {

bool isBlank(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\n';
}

bool isSmallWordChar(unsigned char c) {
  return std::isalnum(c) || c == '_';
}

bool isBigWordChar(unsigned char c) {
  return c != 0 && !isBlank(c) && c != '\n';
}

bool isBlankLineStr(const std::string &s) {
  for (unsigned char c : s) {
    if (c != ' ' && c != '\t')
      return false;
  }
  return true; // empty or whitespace-only
}

bool isSentenceEnd(unsigned char c) {
  return c == '.' || c == '!' || c == '?';
}

int firstNonBlankColInLineStr(const std::string &s) {
  for (int i = 0; i < (int)s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c != ' ' && c != '\t')
      return i;
  }
  return 0;
}

// Char at (line,col):
// - Returns 0 only if line is out of range.
// - Returns '\n' if col is outside the actual line (used as "newline/blank"
//   sentinel for word motions).
unsigned char getChar(const std::vector<std::string> &lines, int line,
                      int col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return 0;
  const auto &ln = lines[line];
  if (col < 0 || col >= static_cast<int>(ln.size()))
    return '\n';
  return static_cast<unsigned char>(ln[col]);
}

// Step forward one character in the logical buffer (across lines).
// Returns false if already at (or past) the last character.
bool stepFwd(const std::vector<std::string> &lines, int &line, int &col) {
  int n = static_cast<int>(lines.size());
  if (line < 0 || line >= n)
    return false;
  const auto &ln = lines[line];
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
bool stepBack(const std::vector<std::string> &lines, int &line, int &col) {
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

// Paragraph helpers

// Returns the first line index of the paragraph containing lineIdx.
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line
// run.
int paragraphStartLine(const std::vector<std::string> &lines, int lineIdx) {
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
// If lineIdx is on blank lines, the "paragraph" is the contiguous blank-line
// run.
int paragraphEndLine(const std::vector<std::string> &lines, int lineIdx) {
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

} // namespace VimUtils

