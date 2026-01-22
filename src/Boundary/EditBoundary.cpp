#include "EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"

using namespace std;

// =============================================================================
// EditBoundary constructors
// =============================================================================

// Primary constructor: analyze boundary from buffer context
EditBoundary::EditBoundary(const Lines &lines, Position firstPos, Position lastPos)
    : prefix_(), suffix_(),
      hasLinesAbove_(false), hasLinesBelow_(false),
      firstLineQuotes_(), lastLineQuotes_(),
      firstLineBrackets_(), lastLineBrackets_() {
  assert(!lines.empty() && "Lines invariant: buffer always has at least one line");

  const string &firstLine = lines[firstPos.line];
  // Extract full prefix: all characters before firstPos on the first line
  if (firstPos.col > 0) {
    prefix_ = firstLine.substr(0, firstPos.col);
  }
  // prefix_ stays empty if at column 0 (atLineStart)

  const string &lastLine = lines[lastPos.line];
  int endSize = static_cast<int>(lastLine.size());
  // Extract full suffix: all characters after lastPos on the last line
  if (lastPos.col + 1 < endSize) {
    suffix_ = lastLine.substr(lastPos.col + 1);
  }
  // suffix_ stays empty if at end of line (atLineEnd)

  hasLinesAbove_ = (firstPos.line > 0);
  hasLinesBelow_ = (lastPos.line + 1 < static_cast<int>(lines.size()));

  // Scan prefix for quotes/brackets (for text object support)
  for (char c : prefix_) {
    firstLineQuotes_.add(c);
    firstLineBrackets_.add(c);
  }

  // Scan suffix for quotes/brackets
  for (char c : suffix_) {
    lastLineQuotes_.add(c);
    lastLineBrackets_.add(c);
  }
}

// Inherited constructor: use parent boundary but refine with new region
EditBoundary::EditBoundary(const EditBoundary &parent, const Lines &lines,
                           Position firstPos, Position lastPos)
    : prefix_(), suffix_(),
      hasLinesAbove_(parent.hasLinesAbove()),
      hasLinesBelow_(parent.hasLinesBelow()),
      firstLineQuotes_(parent.firstLineQuotes()),
      lastLineQuotes_(parent.lastLineQuotes()),
      firstLineBrackets_(parent.firstLineBrackets()),
      lastLineBrackets_(parent.lastLineBrackets()) {
  assert(!lines.empty() && "Lines invariant: buffer always has at least one line");

  const string &firstLine = lines[firstPos.line];
  // Extract full prefix from current lines, or inherit from parent if at edge
  if (firstPos.col > 0) {
    prefix_ = firstLine.substr(0, firstPos.col);
  } else if (firstPos.line > 0) {
    // At column 0 but not first line of these lines - no prefix
    prefix_ = "";
  } else {
    // At (0, 0) of these lines - inherit parent's prefix
    prefix_ = parent.prefix();
  }

  const string &lastLine = lines[lastPos.line];
  int endSize = static_cast<int>(lastLine.size());
  // Extract full suffix from current lines, or inherit from parent if at edge
  if (lastPos.col + 1 < endSize) {
    suffix_ = lastLine.substr(lastPos.col + 1);
  } else if (lastPos.line + 1 < static_cast<int>(lines.size())) {
    // At end of line but not last line of these lines - no suffix
    suffix_ = "";
  } else {
    // At end of last line - inherit parent's suffix
    suffix_ = parent.suffix();
  }

  hasLinesAbove_ = parent.hasLinesAbove() || (firstPos.line > 0);
  hasLinesBelow_ = parent.hasLinesBelow() ||
                  (lastPos.line + 1 < static_cast<int>(lines.size()));

  // Scan prefix for quotes/brackets
  for (char c : prefix_) {
    firstLineQuotes_.add(c);
    firstLineBrackets_.add(c);
  }

  // Scan suffix for quotes/brackets
  for (char c : suffix_) {
    lastLineQuotes_.add(c);
    lastLineBrackets_.add(c);
  }
}
