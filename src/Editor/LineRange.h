#pragma once

#include <algorithm>

// A line-wise region. Always inclusive.
struct LineRange {
  int firstLine;
  int lastLine;

  constexpr LineRange(int f, int l) : firstLine(f), lastLine(l) {}

  void normalize() {
    if (lastLine < firstLine) {
      std::swap(firstLine, lastLine);
    }
  }

  bool isValid() const {
    return firstLine >= 0;
  }
};

// Sentinel value for "line range outside boundary"
constexpr LineRange LINE_RANGE_OUTSIDE_BOUNDARY(-1, -1);
