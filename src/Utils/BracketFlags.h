#pragma once

#include <cstdint>

struct BracketFlags {
  uint8_t flags = 0;

  // Direct mask constants - zero comparisons when type is known
  static constexpr uint8_t Paren  = 1 << 0;  // ()
  static constexpr uint8_t Square = 1 << 1;  // []
  static constexpr uint8_t Curly  = 1 << 2;  // {}
  static constexpr uint8_t Angle  = 1 << 3;  // <>

  // When caller already knows the type
  void add(uint8_t mask) { flags |= mask; }
  bool seen(uint8_t mask) const { return flags & mask; }
  void reset() { flags = 0; }

  // When you have raw char (2 comparisons)
  // Accepts either opener or closer
  static constexpr uint8_t maskFor(char c) {
    return (c == '(' || c == ')') ? Paren
         : (c == '[' || c == ']') ? Square
         : (c == '{' || c == '}') ? Curly
                                  : Curly;
  }
};
