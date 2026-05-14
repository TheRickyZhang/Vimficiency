#pragma once

#include <gtest/gtest.h>

#include <string>

#include "Interpreter/MovementInterpreter.h"
#include "Types/NavContext.h"
#include "Utils/TestUtils.h"
#include "VimCore/VimOptions.h"

class MiscMotionsTest : public ::testing::Test {
protected:
  inline static Lines a1_long_line;
  inline static Lines a2_block_lines;
  inline static NavContext navContext{0, 0};

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    navContext = NavContext();
  }

  static void expectPos(CursorPos actual, int line, int col,
                        const std::string& msg = "") {
    EXPECT_EQ(actual.line, line) << msg << " (line)";
    EXPECT_EQ(actual.col, col) << msg << " (col)";
  }
};

inline CursorPos simulateWithNav(CursorPos start, const std::string& motion,
                                 const Lines& lines, NavContext nav) {
  return simulateMovements(start, motion, lines, nav);
}

inline Lines makeLines(int count) {
  Lines lines;
  for (int i = 0; i < count; i++) {
    lines.push_back("line" + std::to_string(i));
  }
  return lines;
}
