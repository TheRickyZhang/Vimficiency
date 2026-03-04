#include "VimTextObjectsLegacy.h"
#include "VimEditUtils.h"

#include <algorithm>
#include <utility>

using namespace std;

namespace VimTextObjectsLegacy {

// -----------------------------------------------------------------------------
// Quote text objects (i", a", i', a')
// -----------------------------------------------------------------------------

CharRange innerQuote(const Lines& lines, CursorPos pos, char quote) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return CharRange(pos, pos);

  int line = clamp(pos.line, 0, n - 1);
  const string& ln = lines[line];
  int len = static_cast<int>(ln.size());
  if (len == 0) return CharRange(pos, pos);

  int col = clamp(pos.col, 0, len - 1);

  // Find opening quote (searching backward then forward from cursor)
  int openQuote = -1;
  int closeQuote = -1;

  // First, check if we're inside quotes by scanning the line
  // Count quotes to determine if cursor is inside a quoted region
  vector<int> quotePositions;
  for (int i = 0; i < len; i++) {
    if (ln[i] == quote) {
      quotePositions.push_back(i);
    }
  }

  // Find the pair that contains the cursor
  for (size_t i = 0; i + 1 < quotePositions.size(); i += 2) {
    int open = quotePositions[i];
    int close = quotePositions[i + 1];
    if (col >= open && col <= close) {
      openQuote = open;
      closeQuote = close;
      break;
    }
  }

  // If not found inside, try to find quotes after cursor
  if (openQuote == -1) {
    for (size_t i = 0; i + 1 < quotePositions.size(); i += 2) {
      int open = quotePositions[i];
      int close = quotePositions[i + 1];
      if (open > col) {
        openQuote = open;
        closeQuote = close;
        break;
      }
    }
  }

  if (openQuote == -1 || closeQuote == -1 || openQuote >= closeQuote) {
    return CharRange(pos, pos);  // No valid quote pair found
  }

  // Inner: exclude the quotes themselves
  if (closeQuote - openQuote <= 1) {
    // Empty quotes like "" - return invalid/empty range
    return CHAR_RANGE_OUTSIDE_BOUNDARY;
  }

  return CharRange(CursorPos(line, openQuote + 1), CursorPos(line, closeQuote));
}

CharRange aroundQuote(const Lines& lines, CursorPos pos, char quote) {
  CharRange inner = innerQuote(lines, pos, quote);

  // If inner is empty/invalid, return it
  if (!inner.isValid() || inner.isEmpty()) return inner;
  if (inner.begin.line != inner.end.line) return inner;

  int line = inner.begin.line;
  const string& ln = lines[line];

  // Expand to include the quotes
  int startCol = inner.begin.col > 0 ? inner.begin.col - 1 : inner.begin.col;
  int endCol = inner.end.col < static_cast<int>(ln.size())
                   ? inner.end.col + 1
                   : inner.end.col;

  return CharRange(CursorPos(line, startCol), CursorPos(line, endCol));
}

// -----------------------------------------------------------------------------
// Bracket text objects (i(, a(, i{, a{, i[, a[)
// -----------------------------------------------------------------------------

// Helper: find matching bracket, handling nesting
static pair<CursorPos, CursorPos> findMatchingBrackets(
    const Lines& lines, CursorPos pos, char open, char close) {

  int n = static_cast<int>(lines.size());
  CursorPos openPos(-1, -1);
  CursorPos closePos(-1, -1);

  // Search backward for opening bracket
  int depth = 0;
  int line = pos.line;
  int col = pos.col;

  // Check if we're ON an open or close bracket
  if (line >= 0 && line < n && col >= 0 && col < static_cast<int>(lines[line].size())) {
    char c = lines[line][col];
    if (c == open) depth = 1;
    else if (c == close) depth = -1;
  }

  // Search backward for opening bracket
  int searchLine = pos.line;
  int searchCol = pos.col;

  if (depth <= 0) {
    // Need to find opening bracket
    depth = 0;
    while (searchLine >= 0) {
      const string& ln = lines[searchLine];
      int startCol = (searchLine == pos.line) ? searchCol : static_cast<int>(ln.size()) - 1;

      for (int c = startCol; c >= 0; c--) {
        if (ln[c] == close) depth++;
        else if (ln[c] == open) {
          if (depth == 0) {
            openPos = CursorPos(searchLine, c);
            goto foundOpen;
          }
          depth--;
        }
      }
      searchLine--;
    }
  } else {
    openPos = CursorPos(line, col);
  }

foundOpen:
  if (openPos.line < 0) return {CursorPos(-1, -1), CursorPos(-1, -1)};

  // Search forward for closing bracket
  depth = 1;
  searchLine = openPos.line;
  searchCol = openPos.col + 1;

  while (searchLine < n) {
    const string& ln = lines[searchLine];
    int startCol = (searchLine == openPos.line) ? searchCol : 0;

    for (int c = startCol; c < static_cast<int>(ln.size()); c++) {
      if (ln[c] == open) depth++;
      else if (ln[c] == close) {
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

CharRange innerBracket(const Lines& lines, CursorPos pos, char open, char close) {
  auto [openPos, closePos] = findMatchingBrackets(lines, pos, open, close);

  if (openPos.line < 0 || closePos.line < 0) {
    return CharRange(pos, pos);  // No matching brackets found
  }

  // Inner: exclude the brackets
  // Start after open bracket
  CursorPos start = openPos;
  start.setCol(start.col + 1);
  if (start.col >= static_cast<int>(lines[start.line].size())) {
    start.line++;
    start.setCol(0);
  }

  // End is exclusive: at close bracket position.
  CursorPos end = closePos;

  // Handle empty brackets like ()
  if (start >= end) {
    return CHAR_RANGE_OUTSIDE_BOUNDARY;  // Empty
  }

  return CharRange(start, end);
}

CharRange aroundBracket(const Lines& lines, CursorPos pos, char open, char close) {
  auto [openPos, closePos] = findMatchingBrackets(lines, pos, open, close);

  if (openPos.line < 0 || closePos.line < 0) {
    return CharRange(pos, pos);
  }

  return CharRange(openPos, VimCore::onePastOnSameLine(lines, closePos));
}

} // namespace VimTextObjectsLegacy
