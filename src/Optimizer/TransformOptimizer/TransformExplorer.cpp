#include "TransformExplorer.h"

using namespace std;

bool TransformExplorer::inBoundaryRegion(const CursorPos& pos, const Lines& lines) const {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;
  VimCore::WordBoundaryContext boundary = wordBoundaryContext();
  if (pos.line == 0 && pos.col < boundary.contentStartCol(0)) return true;
  return boundary.inSuffixRegion(pos, lines);
}

pair<int, int> TransformExplorer::computeTransformBounds(const Lines& lines, const CursorPos& cursor) const {
  VimCore::WordBoundaryContext boundary = wordBoundaryContext();
  int contentBegin = boundary.contentStartCol(cursor.line);
  int contentEnd = boundary.effectiveLineEnd(
      lines, cursor.line, cursor.line == lines.lastLine());
  return {contentBegin, contentEnd};
}
