#pragma once

#include <cstdint>

struct BracketFlags {
  uint8_t flags = 0;

  static constexpr uint8_t Paren  = 1 << 0;  // ()
  static constexpr uint8_t Square = 1 << 1;  // []
  static constexpr uint8_t Curly  = 1 << 2;  // {}
  static constexpr uint8_t Angle  = 1 << 3;  // <>

  void add(char c) {
    flags |= maskFor(c);
  }
  bool seen(char c) const {
    return flags & maskFor(c);
  }
  void reset() { flags = 0; }

  // When you have raw char (2 comparisons)
  // Accepts either opener or closer
  static constexpr uint8_t maskFor(char c) {
    switch(c) {
      case '(': case ')': return Paren;
      case '[': case ']': return Square;
      case '{': case '}': return Curly;
      case '<': case '>': return Angle;
      default:   return 0;
    }
  }
};
