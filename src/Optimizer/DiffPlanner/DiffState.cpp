#include "DiffState.h"

#include <algorithm>
#include <cassert>

#include "Utils/PrettyText.h"

using namespace std;

ostream& operator<<(ostream& os, const DiffState& d) {
  if (d.isPureInsertion()) {
    os << "ins '" << VF::prettify(d.insertedText) << "'";
  } else if (d.isPureDeletion()) {
    os << "del '" << VF::prettify(d.deletedText) << "'";
  } else {
    os << "'" << VF::prettify(d.deletedText) << "'->'" << VF::prettify(d.insertedText) << "'";
  }
  return os;
}

namespace DiffText {

CursorPos flatIndexToPosition(int idx, string_view flatText) {
  int line = 0;
  int col = 0;
  for (int i = 0; i < idx && i < static_cast<int>(flatText.size()); i++) {
    if (flatText[i] == '\n') {
      line++;
      col = 0;
    } else {
      col++;
    }
  }
  return CursorPos(line, col);
}

CursorPos flatIndexToPosition(int idx, const Lines& lines) {
  int remaining = idx;
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    const int lineLen = static_cast<int>(lines[i].size());
    if (remaining <= lineLen) {
      return CursorPos(i, remaining);
    }
    remaining -= lineLen + 1;
  }
  const int lastLine = static_cast<int>(lines.size()) - 1;
  return CursorPos(lastLine, static_cast<int>(lines[lastLine].size()));
}

int positionToFlatIndex(const CursorPos& pos, const Lines& lines) {
  int idx = 0;
  for (int i = 0; i < pos.line && i < static_cast<int>(lines.size()); i++) {
    idx += static_cast<int>(lines[i].size()) + 1;
  }
  idx += pos.col;
  return idx;
}

CursorPos advancePositionByText(CursorPos pos, string_view text) {
  for (char c : text) {
    if (c == '\n') {
      pos.line++;
      pos.setCol(0);
    } else {
      pos.setCol(pos.col + 1);
    }
  }
  return pos;
}

optional<DiffState> calculateContiguousResidualDiff(
    const Lines& from,
    const Lines& to) {
  const string fromText = from.flatten();
  const string toText = to.flatten();
  if (fromText == toText) return nullopt;

  int prefixLen = 0;
  const int maxPrefix = static_cast<int>(min(fromText.size(), toText.size()));
  while (prefixLen < maxPrefix && fromText[prefixLen] == toText[prefixLen])
    prefixLen++;

  int suffixLen = 0;
  const int maxSuffix =
      static_cast<int>(min(fromText.size(), toText.size())) - prefixLen;
  while (suffixLen < maxSuffix &&
         fromText[static_cast<int>(fromText.size()) - 1 - suffixLen] ==
             toText[static_cast<int>(toText.size()) - 1 - suffixLen]) {
    suffixLen++;
  }

  const int deletedLen =
      static_cast<int>(fromText.size()) - prefixLen - suffixLen;
  const int insertedLen =
      static_cast<int>(toText.size()) - prefixLen - suffixLen;
  string deleted = fromText.substr(prefixLen, deletedLen);
  string inserted = toText.substr(prefixLen, insertedLen);
  CursorPos begin = flatIndexToPosition(prefixLen, fromText);
  CursorPos end = advancePositionByText(begin, deleted);
  return DiffState(
      begin, end, std::move(deleted), std::move(inserted),
      TransformBoundary(from, begin, end));
}

} // namespace DiffText

int OriginalDiffMapper::mapFlatIndex(int originalFlat) const {
  int mapped = originalFlat;
  for (const AppliedSpan& applied : applied_) {
    assert(!(applied.begin < originalFlat && originalFlat < applied.end) &&
           "diffs must not overlap in original-buffer coordinates");
    if (applied.end <= originalFlat) {
      mapped += applied.delta;
    }
  }
  return mapped;
}

DiffState OriginalDiffMapper::mapDiffToCurrent(
    const DiffState& originalDiff,
    const Lines& originalLines,
    const Lines& currentLines) const {
  const bool hadDeletedContent = originalDiff.hasDeletedContent();
  const int originalBegin =
      DiffText::positionToFlatIndex(originalDiff.beginPos, originalLines);
  const int originalEnd = hadDeletedContent
      ? DiffText::positionToFlatIndex(originalDiff.endPos, originalLines)
      : originalBegin;

  CursorPos begin = DiffText::flatIndexToPosition(
      mapFlatIndex(originalBegin), currentLines);
  CursorPos end = hadDeletedContent
      ? DiffText::flatIndexToPosition(mapFlatIndex(originalEnd), currentLines)
      : begin;

  return DiffState(
      begin, end,
      originalDiff.deletedText,
      originalDiff.insertedText,
      TransformBoundary(currentLines, begin, end));
}

void OriginalDiffMapper::recordApplied(
    const DiffState& originalDiff,
    const Lines& originalLines) {
  const int begin =
      DiffText::positionToFlatIndex(originalDiff.beginPos, originalLines);
  const int end = originalDiff.hasDeletedContent()
      ? DiffText::positionToFlatIndex(originalDiff.endPos, originalLines)
      : begin;
  assert(begin <= end && "original diff span must be non-reversed");
  applied_.push_back(AppliedSpan{
      begin,
      end,
      static_cast<int>(originalDiff.insertedText.size()) -
          static_cast<int>(originalDiff.deletedText.size()),
  });
}
