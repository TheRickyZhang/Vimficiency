#pragma once
#include <cstddef>
#include <ostream>

struct Pos {
  int line = 0;
  int col = 0;

  Pos() = default;
  constexpr Pos(int l, int c) : line(l), col(c) {}

  constexpr bool isValid() const { return line >= 0; }

  // Lexicographic ordering on (line, col).
  constexpr bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
  constexpr bool operator!=(const Pos& o) const { return !(*this == o); }
  constexpr bool operator<(const Pos& o) const {
    if (line != o.line) return line < o.line;
    return col < o.col;
  }
  constexpr bool operator>(const Pos& o) const { return o < *this; }
  constexpr bool operator<=(const Pos& o) const { return !(o < *this); }
  constexpr bool operator>=(const Pos& o) const { return !(*this < o); }

  friend std::ostream& operator<<(std::ostream& os, const Pos& p) {
    os << "(" << p.line << ", " << p.col << ")";
    return os;
  }
};

template<>
struct std::hash<Pos> {
  size_t operator()(const Pos& k) const {
    size_t h = k.line;
    h ^= k.col + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};
