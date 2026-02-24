#pragma once

#include <algorithm>

// A line-wise region. Half-open: [beginLine, endLine).
struct LineRange {
  int beginLine;
  int endLine;

  constexpr LineRange(int begin, int end) : beginLine(begin), endLine(end) {}

  void normalize() {
    if (endLine < beginLine) {
      std::swap(beginLine, endLine);
    }
  }

  bool isValid() const {
    return beginLine >= 0 && endLine >= beginLine;
  }

  int lineCount() const {
    return endLine - beginLine;
  }
};

// Sentinel value for "line range outside boundary"
constexpr LineRange LINE_RANGE_OUTSIDE_BOUNDARY(-1, -1);
