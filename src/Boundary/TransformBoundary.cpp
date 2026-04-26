#include "TransformBoundary.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

using namespace std;

const TransformBoundary& TransformBoundary::noParent() {
  static const TransformBoundary instance{};
  return instance;
}

// =============================================================================
// TransformBoundary constructor
// =============================================================================

// Construct from buffer context, optionally inheriting from parent
// endPos is exclusive: one past the last valid cursor position
TransformBoundary::TransformBoundary(const Lines &lines, CursorPos beginPos, CursorPos endPos,
                           const TransformBoundary &parent)
    : prefix_(), suffix_(),
      hasLinesAbove_(parent.hasLinesAbove()),
      hasLinesBelow_(parent.hasLinesBelow()),
      beginLineQuotes_(parent.beginLineQuotes()),
      endLineQuotes_(parent.endLineQuotes()),
      beginLineBrackets_(parent.beginLineBrackets()),
      endLineBrackets_(parent.endLineBrackets()) {
  assert(!lines.empty() && "Lines invariant: buffer always has at least one line");

  const string &beginLine = lines[beginPos.line];
  // Extract full prefix from current lines, or inherit from parent if at edge
  if (beginPos.col > 0) {
    prefix_ = beginLine.substr(0, beginPos.col);
  } else if (beginPos.line > 0) {
    // At column 0 but not first line of these lines - no prefix
    prefix_ = "";
  } else {
    // At (0, 0) of these lines - inherit parent's prefix
    prefix_ = parent.prefix();
  }

  const string &endLine = lines[endPos.line];
  int endSize = static_cast<int>(endLine.size());
  // Extract full suffix from current lines, or inherit from parent if at edge
  if (endPos.col < endSize) {
    suffix_ = endLine.substr(endPos.col);
  } else if (endPos.line + 1 < static_cast<int>(lines.size())) {
    // At end of line but not last line of these lines - no suffix
    suffix_ = "";
  } else {
    // At end of last line - inherit parent's suffix
    suffix_ = parent.suffix();
  }

  hasLinesAbove_ = parent.hasLinesAbove() || (beginPos.line > 0);
  hasLinesBelow_ = parent.hasLinesBelow() ||
                  (endPos.line + 1 < static_cast<int>(lines.size()));

  // Scan prefix for quotes/brackets
  for (char c : prefix_) {
    beginLineQuotes_.add(c);
    beginLineBrackets_.add(c);
  }

  // Scan suffix for quotes/brackets
  for (char c : suffix_) {
    endLineQuotes_.add(c);
    endLineBrackets_.add(c);
  }
}
