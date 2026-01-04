#include "VimTextObjects.h"
#include "VimUtils.h"

#include <algorithm>

using namespace std;
using namespace VimUtils;

namespace VimTextObjects {

// -----------------------------------------------------------------------------
// Word text objects (iw, aw, iW, aW)
// -----------------------------------------------------------------------------

Range innerWord(const vector<string>& lines, Position pos, bool bigWord) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(pos, pos);

  int line = clamp(pos.line, 0, n - 1);
  const string& ln = lines[line];
  int len = static_cast<int>(ln.size());
  if (len == 0) return Range(pos, pos);

  int col = clamp(pos.col, 0, len - 1);

  auto isWordChar = [bigWord](unsigned char c) {
    return bigWord ? isBigWordChar(c) : isSmallWordChar(c);
  };

  unsigned char c = static_cast<unsigned char>(ln[col]);

  // Determine what kind of "word" we're in
  bool onWord = isWordChar(c);
  bool onBlank = isBlank(c);

  // Find start of current word/blank/symbol run
  int startCol = col;
  while (startCol > 0) {
    unsigned char prev = static_cast<unsigned char>(ln[startCol - 1]);
    if (onBlank && !isBlank(prev)) break;
    if (!onBlank && onWord && !isWordChar(prev)) break;
    if (!onBlank && !onWord && (isWordChar(prev) || isBlank(prev))) break;
    startCol--;
  }

  // Find end of current word/blank/symbol run
  int endCol = col;
  while (endCol < len - 1) {
    unsigned char next = static_cast<unsigned char>(ln[endCol + 1]);
    if (onBlank && !isBlank(next)) break;
    if (!onBlank && onWord && !isWordChar(next)) break;
    if (!onBlank && !onWord && (isWordChar(next) || isBlank(next))) break;
    endCol++;
  }

  return Range(Position(line, startCol), Position(line, endCol), false, true);
}

Range aroundWord(const vector<string>& lines, Position pos, bool bigWord) {
  Range inner = innerWord(lines, pos, bigWord);

  int line = inner.start.line;
  const string& ln = lines[line];
  int len = static_cast<int>(ln.size());

  int startCol = inner.start.col;
  int endCol = inner.end.col;

  // Try to include trailing whitespace first
  int trailEnd = endCol;
  while (trailEnd + 1 < len && isBlank(static_cast<unsigned char>(ln[trailEnd + 1]))) {
    trailEnd++;
  }

  if (trailEnd > endCol) {
    // Found trailing whitespace
    return Range(Position(line, startCol), Position(line, trailEnd), false, true);
  }

  // No trailing whitespace, try leading whitespace
  int leadStart = startCol;
  while (leadStart > 0 && isBlank(static_cast<unsigned char>(ln[leadStart - 1]))) {
    leadStart--;
  }

  if (leadStart < startCol) {
    return Range(Position(line, leadStart), Position(line, endCol), false, true);
  }

  // No surrounding whitespace, return inner
  return inner;
}

// -----------------------------------------------------------------------------
// Paragraph text objects (ip, ap)
// -----------------------------------------------------------------------------

Range innerParagraph(const vector<string>& lines, Position pos) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(pos, pos);

  int line = clamp(pos.line, 0, n - 1);

  int startLine = paragraphStartLine(lines, line);
  int endLine = paragraphEndLine(lines, line);

  // For inner paragraph, if on blank lines, just return blank line range
  // If on non-blank paragraph, return just the non-blank lines
  int endCol = lines[endLine].empty() ? 0 : static_cast<int>(lines[endLine].size()) - 1;

  return Range(Position(startLine, 0), Position(endLine, endCol), true, true);
}

Range aroundParagraph(const vector<string>& lines, Position pos) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(pos, pos);

  int line = clamp(pos.line, 0, n - 1);

  int startLine = paragraphStartLine(lines, line);
  int endLine = paragraphEndLine(lines, line);

  bool onBlank = isBlankLineStr(lines[line]);

  if (!onBlank) {
    // Include trailing blank lines
    while (endLine + 1 < n && isBlankLineStr(lines[endLine + 1])) {
      endLine++;
    }
  } else {
    // On blank lines: include following non-blank paragraph
    if (endLine + 1 < n) {
      int nextEnd = paragraphEndLine(lines, endLine + 1);
      endLine = nextEnd;
    }
  }

  int endCol = lines[endLine].empty() ? 0 : static_cast<int>(lines[endLine].size()) - 1;

  return Range(Position(startLine, 0), Position(endLine, endCol), true, true);
}

// -----------------------------------------------------------------------------
// Quote text objects (i", a", i', a')
// -----------------------------------------------------------------------------

Range innerQuote(const vector<string>& lines, Position pos, char quote) {
  int n = static_cast<int>(lines.size());
  if (n == 0) return Range(pos, pos);

  int line = clamp(pos.line, 0, n - 1);
  const string& ln = lines[line];
  int len = static_cast<int>(ln.size());
  if (len == 0) return Range(pos, pos);

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
    return Range(pos, pos);  // No valid quote pair found
  }

  // Inner: exclude the quotes themselves
  if (closeQuote - openQuote <= 1) {
    // Empty quotes like ""
    return Range(Position(line, openQuote + 1), Position(line, openQuote), false, false);
  }

  return Range(Position(line, openQuote + 1), Position(line, closeQuote - 1), false, true);
}

Range aroundQuote(const vector<string>& lines, Position pos, char quote) {
  Range inner = innerQuote(lines, pos, quote);

  // If inner is empty/invalid, return it
  if (inner.start.line != inner.end.line) return inner;

  int line = inner.start.line;
  const string& ln = lines[line];

  // Expand to include the quotes
  int startCol = inner.start.col > 0 ? inner.start.col - 1 : inner.start.col;
  int endCol = inner.end.col < static_cast<int>(ln.size()) - 1 ? inner.end.col + 1 : inner.end.col;

  return Range(Position(line, startCol), Position(line, endCol), false, true);
}

// -----------------------------------------------------------------------------
// Bracket text objects (i(, a(, i{, a{, i[, a[)
// -----------------------------------------------------------------------------

// Helper: find matching bracket, handling nesting
static pair<Position, Position> findMatchingBrackets(
    const vector<string>& lines, Position pos, char open, char close) {

  int n = static_cast<int>(lines.size());
  Position openPos(-1, -1);
  Position closePos(-1, -1);

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
            openPos = Position(searchLine, c);
            goto foundOpen;
          }
          depth--;
        }
      }
      searchLine--;
    }
  } else {
    openPos = Position(line, col);
  }

foundOpen:
  if (openPos.line < 0) return {Position(-1, -1), Position(-1, -1)};

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
          closePos = Position(searchLine, c);
          return {openPos, closePos};
        }
      }
    }
    searchLine++;
  }

  return {Position(-1, -1), Position(-1, -1)};
}

Range innerBracket(const vector<string>& lines, Position pos, char open, char close) {
  auto [openPos, closePos] = findMatchingBrackets(lines, pos, open, close);

  if (openPos.line < 0 || closePos.line < 0) {
    return Range(pos, pos);  // No matching brackets found
  }

  // Inner: exclude the brackets
  // Start after open bracket
  Position start = openPos;
  start.col++;
  if (start.col >= static_cast<int>(lines[start.line].size())) {
    start.line++;
    start.col = 0;
  }

  // End before close bracket
  Position end = closePos;
  if (end.col > 0) {
    end.col--;
  } else if (end.line > 0) {
    end.line--;
    end.col = static_cast<int>(lines[end.line].size()) - 1;
    if (end.col < 0) end.col = 0;
  }

  // Handle empty brackets like ()
  if (start.line > end.line || (start.line == end.line && start.col > end.col)) {
    return Range(closePos, closePos, false, false);  // Empty
  }

  return Range(start, end, false, true);
}

Range aroundBracket(const vector<string>& lines, Position pos, char open, char close) {
  auto [openPos, closePos] = findMatchingBrackets(lines, pos, open, close);

  if (openPos.line < 0 || closePos.line < 0) {
    return Range(pos, pos);
  }

  return Range(openPos, closePos, false, true);
}

} // namespace VimTextObjects
