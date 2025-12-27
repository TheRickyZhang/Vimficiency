#pragma once

struct Position {
  int line = 0;
  int col  = 0;
  int targetCol = 0;
  Position(int l, int c) : line(l), col(c), targetCol(c) {}
  Position(int l, int c, int tc) : line(l), col(c), targetCol(tc) {}

  void setCol(int c) {
    col = targetCol = c;
  }

  bool operator==(const Position& other) {
    return line == other.line && col == other.col && targetCol == other.targetCol;
  }
  bool operator!=(const Position& other) {
    return !(*this == other);
  }
};
