#pragma once

#include <cstddef>
#include "Editor/Position.h"

struct PosKey {
  int line;
  int col;
  PosKey(int line, int col) : line(line), col(col) {};
  PosKey(const Position& pos) : line(pos.line), col(pos.col) {};

  bool operator==(const PosKey& other) const = default; 
  bool operator<(const PosKey& other) const {
    if (line != other.line) return line < other.line;
    return col < other.col;
  }
};

struct PosKeyHash {
  size_t operator()(const PosKey& k) const {
    size_t h = k.line;
    h ^= k.col + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};
