#include "EditSequenceSpan.h"

#include <cassert>

std::vector<std::string> rowsFromEditSpans(
    std::string_view seq,
    std::span<const EditSequenceSpan> spans) {
  std::vector<std::string> rows;
  uint32_t cursor = 0;
  const uint32_t total = static_cast<uint32_t>(seq.size());

  auto pushIfNonEmpty = [&](uint32_t begin, uint32_t end) {
    assert(begin <= end && "row byte range is inverted");
    assert(end <= total && "row byte range exceeds sequence length");
    if (end > begin && end <= total) {
      rows.emplace_back(seq.substr(begin, end - begin));
    }
  };

  for (const EditSequenceSpan& span : spans) {
    assert(cursor <= span.beginByte && "edit spans must be sorted and non-overlapping");
    assert(span.beginByte <= span.endByte && "edit span byte range is inverted");
    assert(span.endByte <= total && "edit span exceeds sequence length");
    pushIfNonEmpty(cursor, span.beginByte);
    pushIfNonEmpty(span.beginByte, span.endByte);
    cursor = span.endByte;
  }
  pushIfNonEmpty(cursor, total);

  return rows;
}
