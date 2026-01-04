#pragma once

#include <vector>
#include <string>

struct Position;

// Character classification functions

namespace VimUtils {
  bool isBlank(unsigned char c);

  bool isSmallWordChar(unsigned char c);

  bool isBigWordChar(unsigned char c);

  bool isBlankLineStr(const std::string &s);

  bool isSentenceEnd(unsigned char c);

  int firstNonBlankColInLineStr(const std::string &s);

  unsigned char getChar(const std::vector<std::string> &lines, int line,
                              int col);

  bool stepFwd(const std::vector<std::string> &lines, int &line,
                      int &col);

  bool stepBack(const std::vector<std::string> &lines, int &line,
                      int &col);

  int paragraphStartLine(const std::vector<std::string> &lines,
                                int lineIdx);

  int paragraphEndLine(const std::vector<std::string> &lines,
                              int lineIdx);

  // TODO: add customizability for some settings, ie word definition, startofline. 
}

