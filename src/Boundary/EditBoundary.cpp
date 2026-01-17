#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

using namespace std;

// =============================================================================
// EditBoundary constructors
// =============================================================================

// Primary constructor: analyze boundary from buffer context
EditBoundary::EditBoundary(const Lines &lines, Position startPos, Position endPos)
    : firstLineQuotes(), lastLineQuotes(),
      firstLineBrackets(), lastLineBrackets() {
  if (lines.empty()) return;

  const string &firstLine = lines[startPos.line];
  if (startPos.col > 0) {
    leftChar = firstLine[startPos.col - 1];
  } else if (startPos.line > 0) {
    leftChar = '\n';
  } else {
    leftChar = NO_CHAR;
  }

  const string &lastLine = lines[endPos.line];
  int endSize = lastLine.size();
  if (endPos.col + 1 < endSize) {
    rightChar = lastLine[endPos.col + 1];
  } else if (endPos.line + 1 < static_cast<int>(lines.size())) {
    rightChar = '\n';
  } else {
    rightChar = NO_CHAR;
  }

  hasLinesAbove = (startPos.line > 0);
  hasLinesBelow = (endPos.line + 1 < static_cast<int>(lines.size()));

  if (!atLineStart()) {
    for (int i = 0; i < startPos.col; i++) {
      firstLineQuotes.add(firstLine[i]);
      firstLineBrackets.add(firstLine[i]);
    }
  }

  if (!atLineEnd()) {
    for (int i = endSize - 1; i > endPos.col; i--) {
      lastLineQuotes.add(lastLine[i]);
      lastLineBrackets.add(lastLine[i]);
    }
  }
}

// Inherited constructor: use parent boundary but refine with new region
EditBoundary::EditBoundary(const EditBoundary &parent, const Lines &lines,
                           Position startPos, Position endPos)
    : firstLineQuotes(parent.firstLineQuotes),
      lastLineQuotes(parent.lastLineQuotes),
      firstLineBrackets(parent.firstLineBrackets),
      lastLineBrackets(parent.lastLineBrackets) {
  if (lines.empty()) return;

  const string &firstLine = lines[startPos.line];
  if (startPos.col > 0) {
    leftChar = firstLine[startPos.col - 1];
  } else if (startPos.line > 0) {
    leftChar = '\n';
  } else {
    leftChar = parent.leftChar;
  }

  const string &lastLine = lines[endPos.line];
  int endSize = lastLine.size();
  if (endPos.col + 1 < endSize) {
    rightChar = lastLine[endPos.col + 1];
  } else if (endPos.line + 1 < static_cast<int>(lines.size())) {
    rightChar = '\n';
  } else {
    rightChar = parent.rightChar;
  }

  hasLinesAbove = parent.hasLinesAbove || (startPos.line > 0);
  hasLinesBelow = parent.hasLinesBelow ||
                  (endPos.line + 1 < static_cast<int>(lines.size()));

  if (!atLineStart()) {
    for (int i = 0; i < startPos.col; i++) {
      firstLineQuotes.add(firstLine[i]);
      firstLineBrackets.add(firstLine[i]);
    }
  }

  if (!atLineEnd()) {
    for (int i = endSize - 1; i > endPos.col; i--) {
      lastLineQuotes.add(lastLine[i]);
      lastLineBrackets.add(lastLine[i]);
    }
  }
}
