#include "EditExplorer.h"

using namespace std;

bool EditExplorer::inBoundaryRegion(const CursorPos& pos, const Lines& lines) const {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;
  if (pos.line == 0 && pos.col < leftColOffset_) return true;
  if (pos.line == lines.lastLine() && rightColOffset_ > 0 &&
      pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset_) {
    return true;
  }
  return false;
}

pair<int, int> EditExplorer::computeEditBounds(const Lines& lines, const CursorPos& cursor) const {
  int contentBegin = (cursor.line == 0) ? leftColOffset_ : 0;
  int contentEnd = static_cast<int>(lines[cursor.line].size());
  if (cursor.line == lines.lastLine() && rightColOffset_ > 0) {
    contentEnd -= rightColOffset_;
  }
  return {contentBegin, contentEnd};
}
