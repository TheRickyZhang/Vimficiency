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
// Using string_view so they satisfy Indexable concept (have .size() and operator[]).
namespace CharPools {
  constexpr std::string_view LETTERS = "abcdef"; // keyword class
  constexpr std::string_view SYMBOLS = ".,";   // non-keyword, non-space
  constexpr std::string_view SPACE = " ";      // space (others include \r, \t)
}


inline std::string randomWord(int len) {
  std::string result;
  result.resize_and_overwrite(len, [](char* s, size_t n){
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick(CharPools::LETTERS);
    }
    return n;
  });
  return result;
}

inline std::string randomProseLine(int len) {
  std::string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for (int i = 0; i < n; i++) {
      s[i] = RandomGen::pick<std::string_view>({
          {80, CharPools::LETTERS},
          {16, CharPools::SPACE},
          {4, CharPools::SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

inline std::string randomLine(int len) {
  std::string line;
  line.resize_and_overwrite(len, [](char* s, int n) {
    for(int i = 0; i < n; i++) {
        s[i] = RandomGen::pick<std::string_view>({
          {60, CharPools::LETTERS},
          {16, CharPools::SPACE},
          {24, CharPools::SYMBOLS},
      });
    }
    return n;
  });
  return line;
}

inline Lines randomLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    result.push_back(randomLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

inline Lines randomProseLines(int numLines, int minLen, int maxLen) {
  Lines result;
  result.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    result.push_back(randomProseLine(RandomGen::range(minLen, maxLen)));
  }
  return result;
}

static Lines randomCodeLines(int numLines, int avgLineLen) {
  Lines result;
  result.reserve(numLines);

  for (int i = 0; i < numLines; i++) {
    if (RandomGen::chance(1, 10)) {
      result.push_back("");
      continue;
    }

    std::string line;
    line += std::string(RandomGen::range(0, 4), ' ');

    for(int i = 0; i < RandomGen::range(1, avgLineLen * 2); i++) {
      line += RandomGen::pick<std::string_view>({
          {3, CharPools::LETTERS},
          {1, CharPools::SYMBOLS},
          {1, CharPools::SPACE},
      });
    }
    result.push_back(line);
  }
  return result;
}

// =============================================================================
// Random Position Generation
// =============================================================================

// Generate a random valid position within the given buffer
inline Position randomPosition(const Lines& lines) {
  int line = RandomGen::range(0, static_cast<int>(lines.size()) - 1);
  int col = lines[line].empty() ? 0 : RandomGen::range(0, static_cast<int>(lines[line].size()) - 1);
  return Position(line, col);
}

inline Position randomFirstPos(const Lines& lines) {
  return Position(0, RandomGen::range(0, lines.front().effectiveSize() - 1));
}

inline Position randomLastPos(const Lines& lines) {
  int lastLine = lines.lastLine();
  return Position(lastLine, RandomGen::range(0, lines[lastLine].effectiveSize() - 1));
}
