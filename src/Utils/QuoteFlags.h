#pragma once

#include <cstdint>


class QuoteFlags {
  uint8_t flags = 0;

  // Direct mask constants - zero comparisons when type is known
  static constexpr uint8_t DoubleQuote = 1 << 0;
  static constexpr uint8_t SingleQuote = 1 << 1;
  static constexpr uint8_t Backtick    = 1 << 2;

  static constexpr uint8_t maskFor(char c) {
    switch(c) {
      case '"':  return DoubleQuote;
      case '\'': return SingleQuote;
      case '`':  return Backtick;
      default:   return 0;
    }
  }

public:
  void add(char c) {
    flags |= maskFor(c);
  }
  bool seen(char c) const {
    return flags & maskFor(c);
  }
  void reset() { flags = 0; }
};
