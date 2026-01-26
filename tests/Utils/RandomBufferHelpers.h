// tests/Utils/RandomBufferHelpers.h
//
// CORE random buffer generation utilities shared across all test suites.
// Uses RandomGen singleton for all randomness.

#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"
#include "Utils/RandomGeneration.h"

#include <string>
#include <string_view>

// Character pools for random buffer generation.
// Use KEYWORDS/SYMBOLS for word boundary testing (character CLASS matters, not specific chars).
// Use ALPHA for realistic content where you need variety.
// Using string_view so they satisfy Indexable concept (have .size() and operator[]).
namespace CharPools {
constexpr std::string_view KEYWORDS = "abcdef";                      // Short keyword chars
constexpr std::string_view ALPHA = "abcdefghijklmnopqrstuvwxyz";     // Full lowercase alphabet
constexpr std::string_view SYMBOLS = ".,";                           // Punctuation (non-keyword, non-space)
constexpr std::string_view SPACE = " ";                              // Single space
}

// =============================================================================
// Random Content Generation
// =============================================================================

// Generate a random word using lowercase letters
inline std::string randomWord(int len) {
  std::string result;
  result.reserve(len);
  for (int i = 0; i < len; i++) {
    result += RandomGen::pick(CharPools::KEYWORDS);
  }
  return result;
}

// Generate a random line with prose-like distribution (mostly letters)
// ~80% letters, ~15% spaces, ~5% symbols
inline std::string randomProseLine(int len) {
  std::string line;
  line.reserve(len);

  for (int i = 0; i < len; i++) {
    line += RandomGen::pick<std::string_view>({
        {16, CharPools::ALPHA},
        {3, CharPools::SPACE},
        {1, CharPools::SYMBOLS},
    });
  }
  return line;
}

// Generate random lines of words with specified count and length range
inline Lines randomLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);

  for (int i = 0; i < numLines; i++) {
    result.push_back(randomWord(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

// Generate random prose lines (for word boundary testing)
inline Lines randomProseLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);

  for (int i = 0; i < numLines; i++) {
    result.push_back(randomProseLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

// =============================================================================
// Random Position Generation
// =============================================================================

// Generate a random valid position within the given buffer
inline Position randomPosition(const Lines& lines) {
  if (lines.empty()) {
    return Position(0, 0);
  }
  int line = RandomGen::range(0, static_cast<int>(lines.size()) - 1);
  int col = lines[line].empty() ? 0 : RandomGen::range(0, static_cast<int>(lines[line].size()) - 1);
  return Position(line, col);
}

// Generate a random position within a specified line range
inline Position randomPositionInRange(const Lines& lines, int startLine, int endLine) {
  if (lines.empty() || startLine > endLine) {
    return Position(0, 0);
  }
  int line = RandomGen::range(startLine, endLine);
  int col = lines[line].empty() ? 0 : RandomGen::range(0, static_cast<int>(lines[line].size()) - 1);
  return Position(line, col);
}
