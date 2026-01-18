#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

using namespace std;

// =============================================================================
// EditBoundary constructors
// =============================================================================

// Primary constructor: analyze boundary from buffer context
EditBoundary::EditBoundary(const Lines &lines, Position startPos, Position endPos)
    : prefix(), suffix(),
      hasLinesAbove(false), hasLinesBelow(false),
      firstLineQuotes(), lastLineQuotes(),
      firstLineBrackets(), lastLineBrackets() {
  if (lines.empty()) return;

  const string &firstLine = lines[startPos.line];
  // Extract full prefix: all characters before startPos on the first line
  if (startPos.col > 0) {
    prefix = firstLine.substr(0, startPos.col);
  }
  // prefix stays empty if at column 0 (atLineStart)

  const string &lastLine = lines[endPos.line];
  int endSize = static_cast<int>(lastLine.size());
  // Extract full suffix: all characters after endPos on the last line
  if (endPos.col + 1 < endSize) {
    suffix = lastLine.substr(endPos.col + 1);
  }
  // suffix stays empty if at end of line (atLineEnd)

  hasLinesAbove = (startPos.line > 0);
  hasLinesBelow = (endPos.line + 1 < static_cast<int>(lines.size()));

  // Scan prefix for quotes/brackets (for text object support)
  for (char c : prefix) {
    firstLineQuotes.add(c);
    firstLineBrackets.add(c);
  }

  // Scan suffix for quotes/brackets
  for (char c : suffix) {
    lastLineQuotes.add(c);
    lastLineBrackets.add(c);
  }
}

// Inherited constructor: use parent boundary but refine with new region
EditBoundary::EditBoundary(const EditBoundary &parent, const Lines &lines,
                           Position startPos, Position endPos)
    : prefix(), suffix(),
      hasLinesAbove(parent.hasLinesAbove),
      hasLinesBelow(parent.hasLinesBelow),
      firstLineQuotes(parent.firstLineQuotes),
      lastLineQuotes(parent.lastLineQuotes),
      firstLineBrackets(parent.firstLineBrackets),
      lastLineBrackets(parent.lastLineBrackets) {
  if (lines.empty()) return;

  const string &firstLine = lines[startPos.line];
  // Extract full prefix from current lines, or inherit from parent if at edge
  if (startPos.col > 0) {
    prefix = firstLine.substr(0, startPos.col);
  } else if (startPos.line > 0) {
    // At column 0 but not first line of these lines - no prefix
    prefix = "";
  } else {
    // At (0, 0) of these lines - inherit parent's prefix
    prefix = parent.prefix;
  }

  const string &lastLine = lines[endPos.line];
  int endSize = static_cast<int>(lastLine.size());
  // Extract full suffix from current lines, or inherit from parent if at edge
  if (endPos.col + 1 < endSize) {
    suffix = lastLine.substr(endPos.col + 1);
  } else if (endPos.line + 1 < static_cast<int>(lines.size())) {
    // At end of line but not last line of these lines - no suffix
    suffix = "";
  } else {
    // At end of last line - inherit parent's suffix
    suffix = parent.suffix;
  }

  hasLinesAbove = parent.hasLinesAbove || (startPos.line > 0);
  hasLinesBelow = parent.hasLinesBelow ||
                  (endPos.line + 1 < static_cast<int>(lines.size()));

  // Scan prefix for quotes/brackets
  for (char c : prefix) {
    firstLineQuotes.add(c);
    firstLineBrackets.add(c);
  }

  // Scan suffix for quotes/brackets
  for (char c : suffix) {
    lastLineQuotes.add(c);
    lastLineBrackets.add(c);
  }
}
