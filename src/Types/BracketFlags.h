#pragma once

#include <array>
#include <cstdint>

class BracketFlags {
  uint8_t flags = 0;

  static constexpr uint8_t Paren = 1 << 0;   // ()
  static constexpr uint8_t Square = 1 << 1;  // []
  static constexpr uint8_t Curly = 1 << 2;   // {}
  static constexpr uint8_t Angle = 1 << 3;   // <>

  static constexpr uint8_t maskFor(char c) {
    switch (c) {
      case '(':
      case ')':
        return Paren;
      case '[':
      case ']':
        return Square;
      case '{':
      case '}':
        return Curly;
      case '<':
      case '>':
        return Angle;
      default:
        return 0;
    }
  }

public:
  void add(char c) { flags |= maskFor(c); }
  bool seen(char c) const { return flags & maskFor(c); }
  void reset() { flags = 0; }

  static constexpr std::array<char, 4> ALL_BRACKETS = {'(', '[', '{', '<'};
};
