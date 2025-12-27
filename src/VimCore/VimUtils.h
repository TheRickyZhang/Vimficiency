#pragma once

#include <vector>
#include <string>

struct Position;

// Classification for characters
/*
* bool isBlank(unsigned char c);
* bool isSmallWordChar(unsigned char c);
* bool isBigWordChar(unsigned char c);
*
* bool isBlankLineStr(const string&);
* int firstNonBlankColInLineStr(const std::string& s);
* 
* unsigned char getChar(const std::vector<std::string> &lines, int line, int col);
* bool stepFwd(const std::vector<std::string> &lines, int &line, int &col);
* bool stepBack(const std::vector<std::string> &lines, int &line, int &col);
*
* int paragraphStartLine(const std::vector<std::string>& lines, int lineIdx);
* int paragraphEndLine(const std::vector<std::string>& lines, int lineIdx);
*
* static bool isSentenceCloser(unsigned char c){
*/

struct VimUtils {
  // Fundamental helpers for working with position
  static int clampCol(const std::vector<std::string> &lines,
                      int col,
                      int lineIdx);
  static void moveCol(Position &pos,
                      const std::vector<std::string> &lines,
                      int dx);
  static void moveLine(Position &pos,
                       const std::vector<std::string> &lines,
                       int dy);



  // Word motions
  static void motion_w(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);

  static void motion_b(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);

  static void motion_e(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);

  // Paragraph motions
  static void moveToParagraphStart(Position& pos, const std::vector<std::string>& lines);
  static void moveToParagraphEnd(Position& pos, const std::vector<std::string>& lines);
  static void motion_paragraphPrev(Position& pos, const std::vector<std::string>& lines);
  static void motion_paragraphNext(Position& pos, const std::vector<std::string>& lines);

  // Sentence motions
  static void motion_sentencePrev(Position& pos, const std::vector<std::string>& lines);
  static void motion_sentenceNext(Position& pos, const std::vector<std::string>& lines);
};
