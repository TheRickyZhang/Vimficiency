#pragma once

#include <string>
#include <string_view>

#include "Types/CursorPos.h"
#include "Types/Lines.h"

inline constexpr std::string_view RANDOM_LETTERS = "abcdef";
inline constexpr std::string_view RANDOM_SYMBOLS = ".,";
inline constexpr std::string_view RANDOM_SPACE = " ";

// Buffer-generation knobs shared by the benchmarks, the explore tool, and the
// optimizer-case catalog — all link test_utils, so this is their common home.
enum class BufferShape { Uniform, Prose, CodeLike };

constexpr int DEFAULT_RANGE_SIZE = 6;
constexpr int DEFAULT_SEED_COUNT = 5;

std::string randomWord(int len);
std::string randomHighSpaceLine(int len);
std::string randomProseLine(int len);
std::string randomLine(int len);
Lines randomLines(int numLines, int minLen, int maxLen);
Lines randomProseLines(int numLines, int minLen, int maxLen);
Lines randomProseBuffer(int targetLines);
Lines randomCodeBuffer(int numLines, int avgLineLen);

inline Lines generateBuffer(int numLines = 20, int avgLineLen = 30,
                            BufferShape shape = BufferShape::CodeLike) {
  switch (shape) {
    case BufferShape::Uniform:
      return randomProseLines(numLines, avgLineLen, avgLineLen);
    case BufferShape::Prose:
      return randomProseBuffer(numLines);
    case BufferShape::CodeLike:
      return randomCodeBuffer(numLines, avgLineLen);
  }
  return {};
}

int randomLineIndex(const Lines& lines);
int randomCol(std::string_view line);
int randomInsertCol(std::string_view line);

CursorPos randomPos(const Lines& lines);
CursorPos randomFirstPos(const Lines& lines);
CursorPos randomLastPos(const Lines& lines);
