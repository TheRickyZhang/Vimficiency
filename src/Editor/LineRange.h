#pragma once

#include <algorithm>

// A line-wise region. Always inclusive.
struct LineRange {
  int startLine;
  int endLine;

  constexpr LineRange(int s, int e) : startLine(s), endLine(e) {}

  void normalize() {
    if (endLine < startLine) {
      std::swap(startLine, endLine);
    }
  }

  bool isValid() const {
    return startLine >= 0;
  }
};

// Sentinel value for "line range outside boundary"
constexpr LineRange LINE_RANGE_OUTSIDE_BOUNDARY(-1, -1);
