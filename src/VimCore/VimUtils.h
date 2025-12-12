#pragma once

#include <vector>
#include <string>

struct Position;

struct VimUtils {
  static int clampCol(const std::vector<std::string> &lines,
                      int col,
                      int lineIdx);

  static void moveCol(Position &pos,
                      const std::vector<std::string> &lines,
                      int dx);

  static void moveLine(Position &pos,
                       const std::vector<std::string> &lines,
                       int dy);

  static unsigned char getChar(const std::vector<std::string> &lines,
                               int line,
                               int col);

  static bool stepFwd(const std::vector<std::string> &lines,
                      int &line,
                      int &col);

  static bool stepBack(const std::vector<std::string> &lines,
                       int &line,
                       int &col);

  static bool isBlank(unsigned char c);
  static bool isSmallWordChar(unsigned char c);
  static bool isBigWordChar(unsigned char c);

  static void motion_w(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);

  static void motion_b(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);

  static void motion_e(Position &pos,
                       const std::vector<std::string> &lines,
                       bool big);
};
