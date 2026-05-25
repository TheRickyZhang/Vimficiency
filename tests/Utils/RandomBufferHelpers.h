#pragma once

#include <string>
#include <string_view>

#include "Types/CursorPos.h"
#include "Types/Lines.h"

namespace CharPools {
constexpr std::string_view LETTERS = "abcdef";
constexpr std::string_view SYMBOLS = ".,";
constexpr std::string_view SPACE = " ";
}  // namespace CharPools

std::string randomWord(int len);
std::string randomHighSpaceLine(int len);
std::string randomProseLine(int len);
std::string randomLine(int len);
Lines randomLines(int numLines, int minLen, int maxLen);
Lines randomProseLines(int numLines, int minLen, int maxLen);
Lines randomProseBuffer(int targetLines);
Lines randomCodeBuffer(int numLines, int avgLineLen);

int randomLineIndex(const Lines& lines);
int randomCol(std::string_view line);
int randomInsertCol(std::string_view line);

CursorPos randomPos(const Lines& lines);
CursorPos randomFirstPos(const Lines& lines);
CursorPos randomLastPos(const Lines& lines);
