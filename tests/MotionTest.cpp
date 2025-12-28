#include <gtest/gtest.h>
#include "TestUtils.h"
#include "Editor/Motion.h"

using namespace std;

// =============================================================================
// Motion Test Suite - Tests apply_motions correctness
// =============================================================================

class MotionTest : public ::testing::Test {
protected:
  static vector<string> a1_long_line;
  static vector<string> a2_block_lines;
  static vector<string> a3_spaced_lines;
  static vector<string> m2_main_big;

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
    m2_main_big = TestFiles::load("m2_main_big.txt");
  }

  // Helper: apply motion and return resulting position
  static Position applyMotion(Position start, const string& motion, const vector<string>& lines) {
    return apply_motions(start, Mode::Normal, motion, lines).pos;
  }

  // Helper: assert position
  static void expectPos(Position actual, int line, int col, const string& msg = "") {
    EXPECT_EQ(actual.line, line) << msg << " (line)";
    EXPECT_EQ(actual.col, col) << msg << " (col)";
  }
};

// Static member definitions
vector<string> MotionTest::a1_long_line;
vector<string> MotionTest::a2_block_lines;
vector<string> MotionTest::a3_spaced_lines;
vector<string> MotionTest::m2_main_big;

// =============================================================================
// 1. BASIC MOTIONS (h, j, k, l)
// =============================================================================

// a1_long_line: "aaaaaa aaa aaaaaa aaa" (len=21)
TEST_F(MotionTest, H_MovesLeft) {
  expectPos(applyMotion({0, 5}, "h", a1_long_line), 0, 4);
  expectPos(applyMotion({0, 5}, "hhh", a1_long_line), 0, 2);
}

TEST_F(MotionTest, H_StopsAtLineStart) {
  expectPos(applyMotion({0, 0}, "h", a1_long_line), 0, 0);
  expectPos(applyMotion({0, 2}, "hhhhh", a1_long_line), 0, 0);
}

TEST_F(MotionTest, L_MovesRight) {
  expectPos(applyMotion({0, 0}, "l", a1_long_line), 0, 1);
  expectPos(applyMotion({0, 0}, "lll", a1_long_line), 0, 3);
}

TEST_F(MotionTest, L_StopsAtLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(applyMotion({0, lastCol}, "l", a1_long_line), 0, lastCol);
}

// a2_block_lines: 4 identical lines of "aaaaaaaaaaaaaa"
TEST_F(MotionTest, J_MovesDown) {
  expectPos(applyMotion({0, 0}, "j", a2_block_lines), 1, 0);
  expectPos(applyMotion({0, 0}, "jj", a2_block_lines), 2, 0);
  expectPos(applyMotion({0, 0}, "jjj", a2_block_lines), 3, 0);
}

TEST_F(MotionTest, J_StopsAtLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  expectPos(applyMotion({lastLine, 0}, "j", a2_block_lines), lastLine, 0);
  expectPos(applyMotion({0, 0}, "jjjjjjjj", a2_block_lines), lastLine, 0);
}

TEST_F(MotionTest, K_MovesUp) {
  expectPos(applyMotion({2, 0}, "k", a2_block_lines), 1, 0);
  expectPos(applyMotion({3, 0}, "kk", a2_block_lines), 1, 0);
}

TEST_F(MotionTest, K_StopsAtFirstLine) {
  expectPos(applyMotion({0, 0}, "k", a2_block_lines), 0, 0);
  expectPos(applyMotion({2, 0}, "kkkkk", a2_block_lines), 0, 0);
}

TEST_F(MotionTest, JK_PreservesColumn) {
  expectPos(applyMotion({0, 5}, "j", a2_block_lines), 1, 5);
  expectPos(applyMotion({0, 5}, "jj", a2_block_lines), 2, 5);
  expectPos(applyMotion({2, 5}, "k", a2_block_lines), 1, 5);
}

TEST_F(MotionTest, JK_ClampsToShorterLine) {
  vector<string> lines = {"long line here", "short", "long line here"};
  expectPos(applyMotion({0, 10}, "j", lines), 1, 4);
  expectPos(applyMotion({0, 10}, "jk", lines), 0, 10);
}

TEST_F(MotionTest, JK_HandlesEmptyLines) {
  vector<string> lines = {"content", "", "content"};
  expectPos(applyMotion({0, 5}, "j", lines), 1, 0);
  expectPos(applyMotion({0, 5}, "jk", lines), 0, 5);
  expectPos(applyMotion({1, 0}, "k", lines), 0, 0);
}

// =============================================================================
// 2. WORD MOTIONS (w, W, b, B, e, E)
// =============================================================================

// a1_long_line: "aaaaaa aaa aaaaaa aaa"
//                0      7   11     18
TEST_F(MotionTest, W_SmallWord_NextWordStart) {
  expectPos(applyMotion({0, 0}, "w", a1_long_line), 0, 7);   // aaaaaa -> aaa
  expectPos(applyMotion({0, 7}, "w", a1_long_line), 0, 11);  // aaa -> aaaaaa
  expectPos(applyMotion({0, 11}, "w", a1_long_line), 0, 18); // aaaaaa -> aaa
}

TEST_F(MotionTest, W_MultipleWords) {
  expectPos(applyMotion({0, 0}, "ww", a1_long_line), 0, 11);
  expectPos(applyMotion({0, 0}, "www", a1_long_line), 0, 18);
}

TEST_F(MotionTest, B_SmallWord_PrevWordStart) {
  //                       0   4   8
  vector<string> lines = {"one two three"};
  expectPos(applyMotion({0, 8}, "b", lines), 0, 4);   // three -> two
  expectPos(applyMotion({0, 4}, "b", lines), 0, 0);   // two -> one
  expectPos(applyMotion({0, 0}, "b", lines), 0, 0);   // one -> stays
}

TEST_F(MotionTest, E_SmallWord_WordEnd) {
  expectPos(applyMotion({0, 0}, "e", a1_long_line), 0, 5);   // end of "aaaaaa"
  expectPos(applyMotion({0, 5}, "e", a1_long_line), 0, 9);   // end of "aaa"
}

// m2_main_big has punctuation for testing W vs w
TEST_F(MotionTest, W_BigWord_SkipsPunctuation) {
  // Line 0: "#include <bits/stdc++.h>"
  // W should skip the whole #include or <bits/stdc++.h>
  Position p = applyMotion({0, 0}, "W", m2_main_big);
  EXPECT_EQ(p.line, 0);
  EXPECT_GT(p.col, 1);  // should skip past #include
}

TEST_F(MotionTest, W_CrossesLines) {
  // From end of a1_long_line (single line), w should stay
  int lastWord = 18;
  Position p = applyMotion({0, lastWord}, "w", a1_long_line);
  // Should stay on same line or go to end
  EXPECT_EQ(p.line, 0);
}

// m2_main_big for multi-line word navigation
TEST_F(MotionTest, W_CrossesLinesInCode) {
  // Navigate from line 0 to line 1
  Position start(0, 0);
  Position p = start;
  int jumps = 0;
  while(p.line == 0 && jumps < 20) {
    p = applyMotion(p, "w", m2_main_big);
    jumps++;
  }
  EXPECT_EQ(p.line, 1) << "w should eventually cross to line 1";
}

TEST_F(MotionTest, WordMotionFromEmptyLine) {
  vector<string> lines = {"", "content"};
  // w from empty line should go to next line's first word
  Position p = applyMotion({0, 0}, "w", lines);
  EXPECT_EQ(p.line, 1);
  EXPECT_EQ(p.col, 0);
}

// =============================================================================
// 3. LINE MOTIONS (0, ^, $)
// =============================================================================

TEST_F(MotionTest, Zero_GoesToColumnZero) {
  expectPos(applyMotion({0, 10}, "0", a1_long_line), 0, 0);
  expectPos(applyMotion({0, 5}, "0", a2_block_lines), 0, 0);
}

TEST_F(MotionTest, Dollar_GoesToLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(applyMotion({0, 0}, "$", a1_long_line), 0, lastCol);
  expectPos(applyMotion({0, 5}, "$", a1_long_line), 0, lastCol);
}

TEST_F(MotionTest, Dollar_OnEmptyLine) {
  vector<string> lines = {"", "content"};
  expectPos(applyMotion({0, 0}, "$", lines), 0, 0);
}

TEST_F(MotionTest, Caret_GoesToFirstNonBlank) {
  vector<string> lines = {"  indented"};
  Position p = applyMotion({0, 5}, "^", lines);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, 2);  // first non-blank after 2 spaces
}

TEST_F(MotionTest, Caret_OnNoIndent) {
  vector<string> lines = {"noindent"};
  Position p = applyMotion({0, 5}, "^", lines);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, 0);
}

// =============================================================================
// 4. PARAGRAPH/SENTENCE MOTIONS ({, }, (, ))
// =============================================================================

TEST_F(MotionTest, CloseBrace_NextParagraph) {
  vector<string> lines = {"para1", "para1", "", "para2", "para2"};
  Position p = applyMotion({0, 0}, "}", lines);
  EXPECT_EQ(p.line, 2);  // blank line
}

TEST_F(MotionTest, OpenBrace_PrevParagraph) {
  vector<string> lines = {"para1", "", "para2", "para2"};
  Position p = applyMotion({3, 0}, "{", lines);
  EXPECT_EQ(p.line, 1);  // blank line
}

TEST_F(MotionTest, MultipleBraceJumps) {
  vector<string> lines = {"a", "", "b", "", "c"};
  Position p1 = applyMotion({0, 0}, "}", lines);
  Position p2 = applyMotion({0, 0}, "}}", lines);
  EXPECT_EQ(p1.line, 1);
  EXPECT_EQ(p2.line, 3);
}

TEST_F(MotionTest, CloseParen_NextSentence) {
  vector<string> lines = {"First. Second."};
  Position start(0, 0);
  Position p = applyMotion(start, ")", lines);
  // Should move to "Second" (after ". ")
  EXPECT_GT(p.col, 0) << ") should move cursor forward";
}

TEST_F(MotionTest, OpenParen_PrevSentence) {
  vector<string> lines = {"First. Second."};
  Position start(0, 10);
  Position p = applyMotion(start, "(", lines);
  EXPECT_LT(p.col, start.col) << "( should move cursor backward";
}

// =============================================================================
// 5. FILE MOTIONS (gg, G)
// =============================================================================

// Neovim has nostartofline by default, so preserve column!
TEST_F(MotionTest, GG_GoesToFirstLine) {
  Position p1 = applyMotion({3, 5}, "gg", a2_block_lines);
  EXPECT_EQ(p1.line, 0);
  EXPECT_EQ(p1.col, 5);
  
  vector<string> lines = {"short", "longer line"};
  Position p2 = applyMotion({1, 8}, "gg", lines);
  EXPECT_EQ(p2.line, 0);
  EXPECT_EQ(p2.col, 4);
}

TEST_F(MotionTest, G_GoesToLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  Position p = applyMotion({0, 5}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
  EXPECT_EQ(p.col, 5);
}

TEST_F(MotionTest, GG_G_RoundTrip) {
  Position start(2, 5);
  Position atTop = applyMotion(start, "gg", a2_block_lines);
  Position atBottom = applyMotion(atTop, "G", a2_block_lines);
  Position backToTop = applyMotion(atBottom, "gg", a2_block_lines);
  
  EXPECT_EQ(atTop.line, 0);
  EXPECT_EQ(atTop.col, 5);
  EXPECT_EQ(atBottom.line, (int)a2_block_lines.size() - 1);
  EXPECT_EQ(atBottom.col, 5);
  EXPECT_EQ(backToTop.line, 0);
  EXPECT_EQ(backToTop.col, 5);
}

// =============================================================================
// 6. COMBINED MOTIONS
// =============================================================================

TEST_F(MotionTest, CombinedHorizontalVertical) {
  // jjl - down 2, right 1
  expectPos(applyMotion({0, 0}, "jjl", a2_block_lines), 2, 1);
  
  // kkhh - up 2, left 2
  expectPos(applyMotion({2, 5}, "kkhh", a2_block_lines), 0, 3);
}

TEST_F(MotionTest, CombinedWordLine) {
  // w$ - next word, then end of line
  Position p = applyMotion({0, 0}, "w$", a1_long_line);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, (int)a1_long_line[0].size() - 1);
}

TEST_F(MotionTest, NavigateCodeBlock) {
  // Navigate through m2_main_big
  // Start at line 0, go to main() line
  Position p = applyMotion({0, 0}, "jj", m2_main_big);  // line 2: "int main() {"
  EXPECT_EQ(p.line, 2);
}

// =============================================================================
// 7. EDGE CASES
// =============================================================================

TEST_F(MotionTest, EmptyLineNavigation) {
  vector<string> lines = {""};
  expectPos(applyMotion({0, 0}, "l", lines), 0, 0);  // can't move right on empty
  expectPos(applyMotion({0, 0}, "h", lines), 0, 0);  // can't move left on empty
  expectPos(applyMotion({0, 0}, "$", lines), 0, 0);  // $ on empty stays
  expectPos(applyMotion({0, 0}, "0", lines), 0, 0);  // 0 on empty stays
}

TEST_F(MotionTest, SingleCharLine) {
  vector<string> lines = {"a"};
  expectPos(applyMotion({0, 0}, "l", lines), 0, 0);
  expectPos(applyMotion({0, 0}, "h", lines), 0, 0);
  expectPos(applyMotion({0, 0}, "$", lines), 0, 0);
  expectPos(applyMotion({0, 0}, "0", lines), 0, 0);
}

TEST_F(MotionTest, FirstAndLastPositions) {
  // gg from (0,0) stays at line 0
  EXPECT_EQ(applyMotion({0, 0}, "gg", a2_block_lines).line, 0);
  
  // G from last line stays at last line
  int lastLine = a2_block_lines.size() - 1;
  Position p = applyMotion({lastLine, 0}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
}

// =============================================================================
// 8. PROPERTY-BASED TESTS (invariants that must always hold)
// =============================================================================

TEST_F(MotionTest, Property_H_NeverIncreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = applyMotion({0, col}, "h", a1_long_line);
    EXPECT_LE(result.col, col) << "h from col " << col << " should not increase col";
    EXPECT_EQ(result.line, 0) << "h should not change line";
  }
}

TEST_F(MotionTest, Property_L_NeverDecreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = applyMotion({0, col}, "l", a1_long_line);
    EXPECT_GE(result.col, col) << "l from col " << col << " should not decrease col";
    EXPECT_EQ(result.line, 0) << "l should not change line";
  }
}

TEST_F(MotionTest, Property_J_NeverDecreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = applyMotion({line, 0}, "j", a2_block_lines);
    EXPECT_GE(result.line, line) << "j from line " << line << " should not decrease line";
  }
}

TEST_F(MotionTest, Property_K_NeverIncreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = applyMotion({line, 0}, "k", a2_block_lines);
    EXPECT_LE(result.line, line) << "k from line " << line << " should not increase line";
  }
}

TEST_F(MotionTest, Property_PositionAlwaysValid) {
  vector<string> motions = {"h", "j", "k", "l", "w", "b", "e", "W", "B", "E",
                            "0", "^", "$", "gg", "G", "{", "}", "(", ")"};
  
  // Test on a2_block_lines
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    for(int col = 0; col < (int)a2_block_lines[line].size(); col++) {
      for(const auto& motion : motions) {
        Position result = applyMotion({line, col}, motion, a2_block_lines);
        
        EXPECT_GE(result.line, 0) << "Negative line for " << motion;
        EXPECT_LT(result.line, (int)a2_block_lines.size()) 
          << "Line out of bounds for " << motion;
        EXPECT_GE(result.col, 0) << "Negative col for " << motion;
        
        if(!a2_block_lines[result.line].empty()) {
          EXPECT_LT(result.col, (int)a2_block_lines[result.line].size())
            << "Col out of bounds for " << motion << " at line " << result.line;
        }
      }
    }
  }
}

TEST_F(MotionTest, Property_GG_AlwaysLine0) {
  for(int line = 0; line < (int)m2_main_big.size(); line++) {
    Position result = applyMotion({line, 5}, "gg", m2_main_big);
    EXPECT_EQ(result.line, 0) << "gg from line " << line << " should go to line 0";
  }
}

TEST_F(MotionTest, Property_G_AlwaysLastLine) {
  int lastLine = m2_main_big.size() - 1;
  for(int line = 0; line < (int)m2_main_big.size(); line++) {
    Position result = applyMotion({line, 0}, "G", m2_main_big);
    EXPECT_EQ(result.line, lastLine) << "G from line " << line << " should go to last line";
  }
}

TEST_F(MotionTest, Property_Zero_AlwaysColumn0) {
  for(int col = 0; col < 15; col++) {
    Position result = applyMotion({0, col}, "0", a1_long_line);
    EXPECT_EQ(result.col, 0) << "0 from col " << col << " should go to col 0";
    EXPECT_EQ(result.line, 0) << "0 should not change line";
  }
}

// =============================================================================
// 9. REALISTIC NAVIGATION SCENARIOS
// =============================================================================

TEST_F(MotionTest, Scenario_JumpToEndAndBack) {
  Position start(3, 5);
  Position end = applyMotion(start, "G", m2_main_big);
  Position back = applyMotion(end, "gg", m2_main_big);
  
  EXPECT_EQ(end.line, (int)m2_main_big.size() - 1);
  EXPECT_EQ(back.line, 0);
}

TEST_F(MotionTest, Scenario_WordNavigationAcrossFile) {
  // Count how many w's to get from start to line 5
  Position p(0, 0);
  int count = 0;
  while(p.line < 5 && count < 50) {
    p = applyMotion(p, "w", m2_main_big);
    count++;
  }
  EXPECT_GE(p.line, 5) << "Should reach line 5 via repeated w";
  EXPECT_LT(count, 50) << "Should not take too many w's";
}
