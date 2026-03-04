#pragma once

#include <cassert>

// A line-wise region. Half-open: [beginLine, endLine).
struct LineRange {
  int beginLine;
  int endLine;

  constexpr LineRange(int begin, int end) : beginLine(begin), endLine(end) {
    assert((beginLine == -1 && endLine == -1)
        || (beginLine >= 0 && endLine >= beginLine));
  }

  bool isValid() const {
    assert((beginLine == -1 && endLine == -1)
        || (beginLine >= 0 && endLine >= beginLine));
    return beginLine >= 0;
  }

  int lineCount() const {
    assert(isValid());
    return endLine - beginLine;
  }
};

// Sentinel value for "line range outside boundary"
constexpr LineRange LINE_RANGE_OUTSIDE_BOUNDARY(-1, -1);
