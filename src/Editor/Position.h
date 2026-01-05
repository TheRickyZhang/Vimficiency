#pragma once
#include <utility>

struct Position {
  int line = 0;
  int col  = 0;
  int targetCol = 0;

  Position() = default;
  Position(int l, int c) : line(l), col(c), targetCol(c) {}
  Position(int l, int c, int tc) : line(l), col(c), targetCol(tc) {}

  // Use setCol() for horizontal movements - updates both col and targetCol.
  // Direct assignment to col (without updating targetCol) should only be used
  // for vertical movements (j/k) that restore col from targetCol.
  void setCol(int c) {
    col = targetCol = c;
  }

  bool operator==(const Position& other) const {
    return line == other.line && col == other.col && targetCol == other.targetCol;
  }
  bool operator!=(const Position& other) const {
    return !(*this == other);
  }
  bool operator<(const Position& other) const {
    if (line != other.line) return line < other.line;
    return col < other.col;
  }
  bool operator>(const Position& other) const {
    return other < *this;
  }
  bool operator<=(const Position& other) const {
    return !(other < *this);
  }
  bool operator>=(const Position& other) const {
    return !(*this < other);
  }
  void swap(Position& other) noexcept {
    std::swap(line, other.line);
    std::swap(col, other.col);
    std::swap(targetCol, other.targetCol);
  }
};
