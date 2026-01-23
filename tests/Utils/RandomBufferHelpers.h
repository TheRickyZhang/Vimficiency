// tests/Utils/RandomBufferHelpers.h
//
// CORE random buffer generation utilities shared across all test suites.
// Domain-specific extensions should build on these primitives.

#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"

#include <random>
#include <string>

// =============================================================================
// Character Pool Constants
// =============================================================================

// Minimal character pools for deterministic word boundaries.
// Word boundaries depend on character CLASS, not specific characters.
namespace CharPools {
constexpr const char* KEYWORDS = "abcdef";  // Keyword characters (letters)
constexpr const char* SYMBOLS = ".,";       // Symbol/punctuation characters
constexpr char SPACE = ' ';                 // Whitespace
}  // namespace CharPools

// =============================================================================
// Random Content Generation
// =============================================================================

// Generate a random word using lowercase letters
inline std::string randomWord(std::mt19937& rng, int len) {
  std::string result;
  result.reserve(len);
  for (int i = 0; i < len; i++) {
    result += CharPools::KEYWORDS[rng() % 6];
  }
  return result;
}

// Generate a random line with mixed character types (keywords, symbols, spaces)
inline std::string randomMixedLine(std::mt19937& rng, int len) {
  std::string line;
  line.reserve(len);
  std::uniform_int_distribution<int> charTypeDist(0, 2);

  for (int i = 0; i < len; i++) {
    int charType = charTypeDist(rng);
    if (charType == 0) {
      line += CharPools::KEYWORDS[rng() % 6];
    } else if (charType == 1) {
      line += CharPools::SYMBOLS[rng() % 2];
    } else {
      line += CharPools::SPACE;
    }
  }
  return line;
}

// Generate random lines with specified count and length range
inline Lines randomLines(std::mt19937& rng, int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  std::uniform_int_distribution<int> lenDist(minLen, maxLen);

  for (int i = 0; i < numLines; i++) {
    result.push_back(randomWord(rng, lenDist(rng)));
  }
  return result;
}

// Generate random lines with mixed character types (for word boundary testing)
inline Lines randomMixedLines(std::mt19937& rng, int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  std::uniform_int_distribution<int> lenDist(minLen, maxLen);

  for (int i = 0; i < numLines; i++) {
    result.push_back(randomMixedLine(rng, lenDist(rng)));
  }
  return result;
}

// =============================================================================
// Random Position Generation
// =============================================================================

// Generate a random valid position within the given buffer
inline Position randomPosition(std::mt19937& rng, const Lines& lines) {
  if (lines.empty()) {
    return Position(0, 0);
  }
  std::uniform_int_distribution<int> lineDist(0, static_cast<int>(lines.size()) - 1);
  int line = lineDist(rng);
  int col = lines[line].empty() ? 0 : static_cast<int>(rng() % lines[line].size());
  return Position(line, col);
}

// Generate a random position within a specified line range
inline Position randomPositionInRange(std::mt19937& rng, const Lines& lines,
                                      int startLine, int endLine) {
  if (lines.empty() || startLine > endLine) {
    return Position(0, 0);
  }
  std::uniform_int_distribution<int> lineDist(startLine, endLine);
  int line = lineDist(rng);
  int col = lines[line].empty() ? 0 : static_cast<int>(rng() % lines[line].size());
  return Position(line, col);
}
