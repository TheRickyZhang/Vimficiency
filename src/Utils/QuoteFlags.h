#pragma once

#include <cstdint>

namespace vimficiency {

struct QuoteFlags {
  uint8_t flags = 0;

  // Direct mask constants - zero comparisons when type is known
  static constexpr uint8_t DoubleQuote = 1 << 0;
  static constexpr uint8_t SingleQuote = 1 << 1;
  static constexpr uint8_t Backtick    = 1 << 2;

  // When caller already knows the type
  void add(uint8_t mask) { flags |= mask; }
  bool seen(uint8_t mask) const { return flags & mask; }
  void reset() { flags = 0; }

  // When you have raw char (2 comparisons)
  static constexpr uint8_t maskFor(char c) {
    return (c == '"') ? DoubleQuote : (c == '\'') ? SingleQuote : Backtick;
  }
};


} // namespace vimficiency
