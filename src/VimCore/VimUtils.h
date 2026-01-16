#pragma once

#include <utility>
#include <vector>
#include <string>

struct Position;

// Character classification functions

namespace VimUtils {
  // Space or tab (not newline) - for within-line whitespace skipping
  bool isWhitespace(unsigned char c);

  // Space, tab, or newline - for general blank checks
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

  // Sentence helpers

  // Check if char is a sentence closer: ) ] " '
  bool isSentenceCloser(unsigned char c);

  // Check if position is a sentence end: [.!?] + optional closers + (whitespace or EOL)
  bool isSentenceEndAt(const std::vector<std::string> &lines, int line, int col);

  // From sentence end, skip past closers and whitespace to find next sentence start.
  // Returns (line, col) of next sentence start.
  std::pair<int, int> skipToSentenceStart(const std::vector<std::string> &lines,
                                          int line, int col);

  // Find the start of the sentence containing position (line, col).
  // Returns (line, col) of sentence start.
  std::pair<int, int> findCurrentSentenceStart(const std::vector<std::string> &lines,
                                               int line, int col);

  // TODO: add customizability for some settings, ie word definition, startofline.
}

