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

// TODO: add customizability for some settings, ie word definition, startofline. 

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
  static void motionW(Position &pos,
                      const std::vector<std::string> &lines,
                      bool big);

  static void motionB(Position &pos,
                      const std::vector<std::string> &lines,
                      bool big);

  static void motionE(Position &pos,
                      const std::vector<std::string> &lines,
                      bool big);

  // Paragraph motions
  static void moveToParagraphStart(Position& pos, const std::vector<std::string>& lines);
  static void moveToParagraphEnd(Position& pos, const std::vector<std::string>& lines);
  static void motionParagraphPrev(Position& pos, const std::vector<std::string>& lines);
  static void motionParagraphNext(Position& pos, const std::vector<std::string>& lines);

  // Sentence motions
  static void motionSentencePrev(Position& pos, const std::vector<std::string>& lines);
  static void motionSentenceNext(Position& pos, const std::vector<std::string>& lines);

  // Character find motions (f/F/t/T)
  // Returns destination column, or -1 if target not found
  // forward: true for f/t, false for F/T
  // till: true for t/T (stop one short), false for f/F (land on target)
  static int findCharInLine(char target, const std::string& line, int startCol, bool forward, bool till);

  template<bool Forward>
  static std::vector<std::tuple<char, int, int>> generateFMotions(int currCol, int targetCol, const std::string& line, int threshold);

};
