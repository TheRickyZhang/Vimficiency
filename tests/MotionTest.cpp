#include <gtest/gtest.h>
#include "TestUtils.h"
#include "Editor/Motion.h"
#include "Editor/NavContext.h"

using namespace std;

// =============================================================================
// Motion Test Suite - Tests applyMotions correctness
// =============================================================================

class MotionTest : public ::testing::Test {
protected:
  static vector<string> a1_long_line;
  static vector<string> a2_block_lines;
  static vector<string> a3_spaced_lines;
  static vector<string> m2_main_big;
  static NavContext navContext;

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
    m2_main_big = TestFiles::load("m2_main_big.txt");
    // Currently the default in my neovim
    navContext = NavContext(0, 39, 39, 19);
  }

  // Helper: apply motion and return resulting position
  static Position simulateMotionsDefault(Position start, const string& motion, const vector<string>& lines) {
    return simulateMotions(start, Mode::Normal, navContext, motion, lines).pos;
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
NavContext MotionTest::navContext(0, 0, 0, 0);

// =============================================================================
// 1. BASIC MOTIONS (h, j, k, l)
// =============================================================================

// a1_long_line: "aaaaaa aaa aaaaaa aaa" (len=21)
TEST_F(MotionTest, H_MovesLeft) {
  expectPos(simulateMotionsDefault({0, 5}, "h", a1_long_line), 0, 4);
  expectPos(simulateMotionsDefault({0, 5}, "hhh", a1_long_line), 0, 2);
}

TEST_F(MotionTest, H_StopsAtLineStart) {
  expectPos(simulateMotionsDefault({0, 0}, "h", a1_long_line), 0, 0);
  expectPos(simulateMotionsDefault({0, 2}, "hhhhh", a1_long_line), 0, 0);
}

TEST_F(MotionTest, L_MovesRight) {
  expectPos(simulateMotionsDefault({0, 0}, "l", a1_long_line), 0, 1);
  expectPos(simulateMotionsDefault({0, 0}, "lll", a1_long_line), 0, 3);
}

TEST_F(MotionTest, L_StopsAtLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(simulateMotionsDefault({0, lastCol}, "l", a1_long_line), 0, lastCol);
}

// a2_block_lines: 4 identical lines of "aaaaaaaaaaaaaa"
TEST_F(MotionTest, J_MovesDown) {
  expectPos(simulateMotionsDefault({0, 0}, "j", a2_block_lines), 1, 0);
  expectPos(simulateMotionsDefault({0, 0}, "jj", a2_block_lines), 2, 0);
  expectPos(simulateMotionsDefault({0, 0}, "jjj", a2_block_lines), 3, 0);
}

TEST_F(MotionTest, J_StopsAtLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  expectPos(simulateMotionsDefault({lastLine, 0}, "j", a2_block_lines), lastLine, 0);
  expectPos(simulateMotionsDefault({0, 0}, "jjjjjjjj", a2_block_lines), lastLine, 0);
}

TEST_F(MotionTest, K_MovesUp) {
  expectPos(simulateMotionsDefault({2, 0}, "k", a2_block_lines), 1, 0);
  expectPos(simulateMotionsDefault({3, 0}, "kk", a2_block_lines), 1, 0);
}

TEST_F(MotionTest, K_StopsAtFirstLine) {
  expectPos(simulateMotionsDefault({0, 0}, "k", a2_block_lines), 0, 0);
  expectPos(simulateMotionsDefault({2, 0}, "kkkkk", a2_block_lines), 0, 0);
}

TEST_F(MotionTest, JK_PreservesColumn) {
  expectPos(simulateMotionsDefault({0, 5}, "j", a2_block_lines), 1, 5);
  expectPos(simulateMotionsDefault({0, 5}, "jj", a2_block_lines), 2, 5);
  expectPos(simulateMotionsDefault({2, 5}, "k", a2_block_lines), 1, 5);
}

TEST_F(MotionTest, JK_ClampsToShorterLine) {
  vector<string> lines = {"long line here", "short", "long line here"};
  expectPos(simulateMotionsDefault({0, 10}, "j", lines), 1, 4);
  expectPos(simulateMotionsDefault({0, 10}, "jk", lines), 0, 10);
}

TEST_F(MotionTest, JK_HandlesEmptyLines) {
  vector<string> lines = {"content", "", "content"};
  expectPos(simulateMotionsDefault({0, 5}, "j", lines), 1, 0);
  expectPos(simulateMotionsDefault({0, 5}, "jk", lines), 0, 5);
  expectPos(simulateMotionsDefault({1, 0}, "k", lines), 0, 0);
}

// =============================================================================
// 2. WORD MOTIONS (w, W, b, B, e, E)
// =============================================================================

// a1_long_line: "aaaaaa aaa aaaaaa aaa"
//                0      7   11     18
TEST_F(MotionTest, W_SmallWord_NextWordStart) {
  expectPos(simulateMotionsDefault({0, 0}, "w", a1_long_line), 0, 7);   // aaaaaa -> aaa
  expectPos(simulateMotionsDefault({0, 7}, "w", a1_long_line), 0, 11);  // aaa -> aaaaaa
  expectPos(simulateMotionsDefault({0, 11}, "w", a1_long_line), 0, 18); // aaaaaa -> aaa
}

TEST_F(MotionTest, W_MultipleWords) {
  expectPos(simulateMotionsDefault({0, 0}, "ww", a1_long_line), 0, 11);
  expectPos(simulateMotionsDefault({0, 0}, "www", a1_long_line), 0, 18);
}

TEST_F(MotionTest, B_SmallWord_PrevWordStart) {
  //                       0   4   8
  vector<string> lines = {"one two three"};
  expectPos(simulateMotionsDefault({0, 8}, "b", lines), 0, 4);   // three -> two
  expectPos(simulateMotionsDefault({0, 4}, "b", lines), 0, 0);   // two -> one
  expectPos(simulateMotionsDefault({0, 0}, "b", lines), 0, 0);   // one -> stays
}

TEST_F(MotionTest, E_SmallWord_WordEnd) {
  expectPos(simulateMotionsDefault({0, 0}, "e", a1_long_line), 0, 5);   // end of "aaaaaa"
  expectPos(simulateMotionsDefault({0, 5}, "e", a1_long_line), 0, 9);   // end of "aaa"
}

// m2_main_big has punctuation for testing W vs w
TEST_F(MotionTest, W_BigWord_SkipsPunctuation) {
  // Line 0: "#include <bits/stdc++.h>"
  // W should skip the whole #include or <bits/stdc++.h>
  Position p = simulateMotionsDefault({0, 0}, "W", m2_main_big);
  EXPECT_EQ(p.line, 0);
  EXPECT_GT(p.col, 1);  // should skip past #include
}

TEST_F(MotionTest, W_CrossesLines) {
  // From end of a1_long_line (single line), w should stay
  int lastWord = 18;
  Position p = simulateMotionsDefault({0, lastWord}, "w", a1_long_line);
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
    p = simulateMotionsDefault(p, "w", m2_main_big);
    jumps++;
  }
  EXPECT_EQ(p.line, 1) << "w should eventually cross to line 1";
}

TEST_F(MotionTest, WordMotionFromEmptyLine) {
  vector<string> lines = {"", "content"};
  // w from empty line should go to next line's first word
  Position p = simulateMotionsDefault({0, 0}, "w", lines);
  EXPECT_EQ(p.line, 1);
  EXPECT_EQ(p.col, 0);
}

// =============================================================================
// 3. LINE MOTIONS (0, ^, $)
// =============================================================================

TEST_F(MotionTest, Zero_GoesToColumnZero) {
  expectPos(simulateMotionsDefault({0, 10}, "0", a1_long_line), 0, 0);
  expectPos(simulateMotionsDefault({0, 5}, "0", a2_block_lines), 0, 0);
}

TEST_F(MotionTest, Dollar_GoesToLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(simulateMotionsDefault({0, 0}, "$", a1_long_line), 0, lastCol);
  expectPos(simulateMotionsDefault({0, 5}, "$", a1_long_line), 0, lastCol);
}

TEST_F(MotionTest, Dollar_OnEmptyLine) {
  vector<string> lines = {"", "content"};
  expectPos(simulateMotionsDefault({0, 0}, "$", lines), 0, 0);
}

TEST_F(MotionTest, Caret_GoesToFirstNonBlank) {
  vector<string> lines = {"  indented"};
  Position p = simulateMotionsDefault({0, 5}, "^", lines);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, 2);  // first non-blank after 2 spaces
}

TEST_F(MotionTest, Caret_OnNoIndent) {
  vector<string> lines = {"noindent"};
  Position p = simulateMotionsDefault({0, 5}, "^", lines);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, 0);
}

// =============================================================================
// 4. PARAGRAPH/SENTENCE MOTIONS ({, }, (, ))
// =============================================================================

TEST_F(MotionTest, CloseBrace_NextParagraph) {
  vector<string> lines = {"para1", "para1", "", "para2", "para2"};
  Position p = simulateMotionsDefault({0, 0}, "}", lines);
  EXPECT_EQ(p.line, 2);  // blank line
}

TEST_F(MotionTest, OpenBrace_PrevParagraph) {
  vector<string> lines = {"para1", "", "para2", "para2"};
  Position p = simulateMotionsDefault({3, 0}, "{", lines);
  EXPECT_EQ(p.line, 1);  // blank line
}

TEST_F(MotionTest, MultipleBraceJumps) {
  vector<string> lines = {"a", "", "b", "", "c"};
  Position p1 = simulateMotionsDefault({0, 0}, "}", lines);
  Position p2 = simulateMotionsDefault({0, 0}, "}}", lines);
  EXPECT_EQ(p1.line, 1);
  EXPECT_EQ(p2.line, 3);
}

TEST_F(MotionTest, CloseParen_NextSentence) {
  vector<string> lines = {"First. Second."};
  Position start(0, 0);
  Position p = simulateMotionsDefault(start, ")", lines);
  // Should move to "Second" (after ". ")
  EXPECT_GT(p.col, 0) << ") should move cursor forward";
}

TEST_F(MotionTest, OpenParen_PrevSentence) {
  vector<string> lines = {"First. Second."};
  Position start(0, 10);
  Position p = simulateMotionsDefault(start, "(", lines);
  EXPECT_LT(p.col, start.col) << "( should move cursor backward";
}

// =============================================================================
// 5. FILE MOTIONS (gg, G)
// =============================================================================

// Neovim has nostartofline by default, so preserve column!
TEST_F(MotionTest, GG_GoesToFirstLine) {
  Position p1 = simulateMotionsDefault({3, 5}, "gg", a2_block_lines);
  EXPECT_EQ(p1.line, 0);
  EXPECT_EQ(p1.col, 5);
  
  vector<string> lines = {"short", "longer line"};
  Position p2 = simulateMotionsDefault({1, 8}, "gg", lines);
  EXPECT_EQ(p2.line, 0);
  EXPECT_EQ(p2.col, 4);
}

TEST_F(MotionTest, G_GoesToLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  Position p = simulateMotionsDefault({0, 5}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
  EXPECT_EQ(p.col, 5);
}

TEST_F(MotionTest, GG_G_RoundTrip) {
  Position start(2, 5);
  Position atTop = simulateMotionsDefault(start, "gg", a2_block_lines);
  Position atBottom = simulateMotionsDefault(atTop, "G", a2_block_lines);
  Position backToTop = simulateMotionsDefault(atBottom, "gg", a2_block_lines);
  
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
  expectPos(simulateMotionsDefault({0, 0}, "jjl", a2_block_lines), 2, 1);
  
  // kkhh - up 2, left 2
  expectPos(simulateMotionsDefault({2, 5}, "kkhh", a2_block_lines), 0, 3);
}

TEST_F(MotionTest, CombinedWordLine) {
  // w$ - next word, then end of line
  Position p = simulateMotionsDefault({0, 0}, "w$", a1_long_line);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, (int)a1_long_line[0].size() - 1);
}

TEST_F(MotionTest, NavigateCodeBlock) {
  // Navigate through m2_main_big
  // Start at line 0, go to main() line
  Position p = simulateMotionsDefault({0, 0}, "jj", m2_main_big);  // line 2: "int main() {"
  EXPECT_EQ(p.line, 2);
}

// =============================================================================
// 7. EDGE CASES
// =============================================================================

TEST_F(MotionTest, EmptyLineNavigation) {
  vector<string> lines = {""};
  expectPos(simulateMotionsDefault({0, 0}, "l", lines), 0, 0);  // can't move right on empty
  expectPos(simulateMotionsDefault({0, 0}, "h", lines), 0, 0);  // can't move left on empty
  expectPos(simulateMotionsDefault({0, 0}, "$", lines), 0, 0);  // $ on empty stays
  expectPos(simulateMotionsDefault({0, 0}, "0", lines), 0, 0);  // 0 on empty stays
}

TEST_F(MotionTest, SingleCharLine) {
  vector<string> lines = {"a"};
  expectPos(simulateMotionsDefault({0, 0}, "l", lines), 0, 0);
  expectPos(simulateMotionsDefault({0, 0}, "h", lines), 0, 0);
  expectPos(simulateMotionsDefault({0, 0}, "$", lines), 0, 0);
  expectPos(simulateMotionsDefault({0, 0}, "0", lines), 0, 0);
}

TEST_F(MotionTest, FirstAndLastPositions) {
  // gg from (0,0) stays at line 0
  EXPECT_EQ(simulateMotionsDefault({0, 0}, "gg", a2_block_lines).line, 0);
  
  // G from last line stays at last line
  int lastLine = a2_block_lines.size() - 1;
  Position p = simulateMotionsDefault({lastLine, 0}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
}

// =============================================================================
// 8. PROPERTY-BASED TESTS (invariants that must always hold)
// =============================================================================

TEST_F(MotionTest, Property_H_NeverIncreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = simulateMotionsDefault({0, col}, "h", a1_long_line);
    EXPECT_LE(result.col, col) << "h from col " << col << " should not increase col";
    EXPECT_EQ(result.line, 0) << "h should not change line";
  }
}

TEST_F(MotionTest, Property_L_NeverDecreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = simulateMotionsDefault({0, col}, "l", a1_long_line);
    EXPECT_GE(result.col, col) << "l from col " << col << " should not decrease col";
    EXPECT_EQ(result.line, 0) << "l should not change line";
  }
}

TEST_F(MotionTest, Property_J_NeverDecreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = simulateMotionsDefault({line, 0}, "j", a2_block_lines);
    EXPECT_GE(result.line, line) << "j from line " << line << " should not decrease line";
  }
}

TEST_F(MotionTest, Property_K_NeverIncreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = simulateMotionsDefault({line, 0}, "k", a2_block_lines);
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
        Position result = simulateMotionsDefault({line, col}, motion, a2_block_lines);
        
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
    Position result = simulateMotionsDefault({line, 5}, "gg", m2_main_big);
    EXPECT_EQ(result.line, 0) << "gg from line " << line << " should go to line 0";
  }
}

TEST_F(MotionTest, Property_G_AlwaysLastLine) {
  int lastLine = m2_main_big.size() - 1;
  for(int line = 0; line < (int)m2_main_big.size(); line++) {
    Position result = simulateMotionsDefault({line, 0}, "G", m2_main_big);
    EXPECT_EQ(result.line, lastLine) << "G from line " << line << " should go to last line";
  }
}

TEST_F(MotionTest, Property_Zero_AlwaysColumn0) {
  for(int col = 0; col < 15; col++) {
    Position result = simulateMotionsDefault({0, col}, "0", a1_long_line);
    EXPECT_EQ(result.col, 0) << "0 from col " << col << " should go to col 0";
    EXPECT_EQ(result.line, 0) << "0 should not change line";
  }
}

// =============================================================================
// 9. REALISTIC NAVIGATION SCENARIOS
// =============================================================================

TEST_F(MotionTest, Scenario_JumpToEndAndBack) {
  Position start(3, 5);
  Position end = simulateMotionsDefault(start, "G", m2_main_big);
  Position back = simulateMotionsDefault(end, "gg", m2_main_big);
  
  EXPECT_EQ(end.line, (int)m2_main_big.size() - 1);
  EXPECT_EQ(back.line, 0);
}

TEST_F(MotionTest, Scenario_WordNavigationAcrossFile) {
  // Count how many w's to get from start to line 5
  Position p(0, 0);
  int count = 0;
  while(p.line < 5 && count < 50) {
    p = simulateMotionsDefault(p, "w", m2_main_big);
    count++;
  }
  EXPECT_GE(p.line, 5) << "Should reach line 5 via repeated w";
  EXPECT_LT(count, 50) << "Should not take too many w's";
}

// =============================================================================
// 10. CHARACTER FIND MOTIONS (f, F, t, T, ;, ,)
// =============================================================================

// Test line: "abcdefghij" (positions 0-9)
// Test line with spaces: "abc def ghi" (positions 0-10)

TEST_F(MotionTest, F_FindForward_Basic) {
  vector<string> lines = {"abcdefghij"};
  expectPos(simulateMotionsDefault({0, 0}, "fc", lines), 0, 2, "f from 0 to 'c'");
  expectPos(simulateMotionsDefault({0, 0}, "fj", lines), 0, 9, "f from 0 to 'j'");
  expectPos(simulateMotionsDefault({0, 3}, "fg", lines), 0, 6, "f from 3 to 'g'");
}

TEST_F(MotionTest, F_FindForward_NotFound) {
  vector<string> lines = {"abcdefghij"};
  // Target not found - position should stay unchanged
  expectPos(simulateMotionsDefault({0, 0}, "fz", lines), 0, 0, "f to nonexistent char");
  expectPos(simulateMotionsDefault({0, 5}, "fa", lines), 0, 5, "f forward to char behind cursor");
}

TEST_F(MotionTest, F_FindBackward_Basic) {
  vector<string> lines = {"abcdefghij"};
  expectPos(simulateMotionsDefault({0, 9}, "Fc", lines), 0, 2, "F from 9 to 'c'");
  expectPos(simulateMotionsDefault({0, 9}, "Fa", lines), 0, 0, "F from 9 to 'a'");
  expectPos(simulateMotionsDefault({0, 6}, "Fd", lines), 0, 3, "F from 6 to 'd'");
}

TEST_F(MotionTest, F_FindBackward_NotFound) {
  vector<string> lines = {"abcdefghij"};
  expectPos(simulateMotionsDefault({0, 9}, "Fz", lines), 0, 9, "F to nonexistent char");
  expectPos(simulateMotionsDefault({0, 3}, "Fj", lines), 0, 3, "F backward to char ahead of cursor");
}

TEST_F(MotionTest, T_TillForward_Basic) {
  vector<string> lines = {"abcdefghij"};
  // t lands ONE BEFORE the target
  expectPos(simulateMotionsDefault({0, 0}, "tc", lines), 0, 1, "t from 0 to before 'c'");
  expectPos(simulateMotionsDefault({0, 0}, "tj", lines), 0, 8, "t from 0 to before 'j'");
  expectPos(simulateMotionsDefault({0, 3}, "tg", lines), 0, 5, "t from 3 to before 'g'");
}

TEST_F(MotionTest, T_TillForward_AdjacentChar) {
  vector<string> lines = {"abcdefghij"};
  // t to adjacent char - lands on current position (one before target)
  expectPos(simulateMotionsDefault({0, 0}, "tb", lines), 0, 0, "t to adjacent char stays");
}

TEST_F(MotionTest, T_TillBackward_Basic) {
  vector<string> lines = {"abcdefghij"};
  // T lands ONE AFTER the target (going backward)
  expectPos(simulateMotionsDefault({0, 9}, "Tc", lines), 0, 3, "T from 9 to after 'c'");
  expectPos(simulateMotionsDefault({0, 9}, "Ta", lines), 0, 1, "T from 9 to after 'a'");
}

TEST_F(MotionTest, T_TillBackward_AdjacentChar) {
  vector<string> lines = {"abcdefghij"};
  expectPos(simulateMotionsDefault({0, 5}, "Te", lines), 0, 5, "T to adjacent char stays");
}

TEST_F(MotionTest, CharFind_WithSemicolonRepeat) {
  vector<string> lines = {"abcabcabc"};  // 'a' at 0, 3, 6; 'b' at 1, 4, 7; 'c' at 2, 5, 8
  expectPos(simulateMotionsDefault({0, 0}, "fa", lines), 0, 3, "fa from 0");
  expectPos(simulateMotionsDefault({0, 0}, "fa;", lines), 0, 6, "fa; from 0");
  expectPos(simulateMotionsDefault({0, 8}, "Fa", lines), 0, 6, "Fa from 8");
  expectPos(simulateMotionsDefault({0, 8}, "Fa;", lines), 0, 3, "Fa; from 8");
  expectPos(simulateMotionsDefault({0, 8}, "Fa;;", lines), 0, 0, "Fa;; from 8");
}

TEST_F(MotionTest, CharFind_WithCommaRepeat) {
  vector<string> lines = {"abcabcabc"};
  expectPos(simulateMotionsDefault({0, 0}, "fa,", lines), 0, 0, "fa, from 0 - back to start");
  expectPos(simulateMotionsDefault({0, 0}, "fa;,", lines), 0, 3, "fa;, from 0 - forward twice, back once");
  expectPos(simulateMotionsDefault({0, 8}, "Fa,", lines), 0, 6, "Fa, from 8 - stays at 6 (no 'a' ahead of 6)");
}

TEST_F(MotionTest, CharFind_MixedRepeat) {
  vector<string> lines = {"abcabcabc"};
  // 0 -> 3 (fa) -> 6 (;) -> 3 (,) -> 6 (;)
  expectPos(simulateMotionsDefault({0, 0}, "fa;,;", lines), 0, 6, "fa;,; complex repeat");
}

TEST_F(MotionTest, CharFind_TillWithRepeat) {
  vector<string> lines = {"abcabcabc"};
  // ta - till 'a', lands one before
  expectPos(simulateMotionsDefault({0, 0}, "ta", lines), 0, 2, "ta from 0 - before first 'a' at 3");
  // This is correct - t; can get "stuck" when you're right before the target
  expectPos(simulateMotionsDefault({0, 0}, "ta;", lines), 0, 2, "ta; from 0 - stuck at 2 (next 'a' is at 3)");
  // fa; uses f behavior for repeats (landing on target), progressing each time
  expectPos(simulateMotionsDefault({0, 0}, "fa;", lines), 0, 6, "fa; from 0 advances properly");
}

TEST_F(MotionTest, CharFind_SpaceAsTarget) {
  vector<string> lines = {"abc def ghi"};  // spaces at 3, 7
  expectPos(simulateMotionsDefault({0, 0}, "f ", lines), 0, 3, "f<space> from 0");
  expectPos(simulateMotionsDefault({0, 0}, "f ;", lines), 0, 7, "f<space>; from 0");
  expectPos(simulateMotionsDefault({0, 0}, "t ", lines), 0, 2, "t<space> from 0");
  expectPos(simulateMotionsDefault({0, 10}, "F ", lines), 0, 7, "F<space> from end");
  expectPos(simulateMotionsDefault({0, 10}, "F ;", lines), 0, 3, "F<space>; from end");
}

TEST_F(MotionTest, CharFind_MultipleOccurrences) {
  vector<string> lines = {"aaaaaa"};
  expectPos(simulateMotionsDefault({0, 0}, "fa", lines), 0, 1, "fa in all-a line");
  expectPos(simulateMotionsDefault({0, 0}, "fa;", lines), 0, 2, "fa; in all-a line");
  expectPos(simulateMotionsDefault({0, 0}, "fa;;", lines), 0, 3, "fa;; in all-a line");
  expectPos(simulateMotionsDefault({0, 0}, "fa;;;", lines), 0, 4, "fa;;; in all-a line");
}

TEST_F(MotionTest, CharFind_CombinedWithOtherMotions) {
  vector<string> lines = {"abcdefghij", "0123456789"};
  expectPos(simulateMotionsDefault({0, 0}, "jfc", lines), 1, 0, "j then fc (no 'c' in line 1)");
  expectPos(simulateMotionsDefault({0, 0}, "fcj", lines), 1, 2, "fc then j");
}

TEST_F(MotionTest, CharFind_AtLineEnd) {
  vector<string> lines = {"abcdef"};
  expectPos(simulateMotionsDefault({0, 5}, "fa", lines), 0, 5, "f from end - target behind");
  expectPos(simulateMotionsDefault({0, 0}, "Ff", lines), 0, 0, "F from start - target ahead");
}

TEST_F(MotionTest, CharFind_OnTargetChar) {
  vector<string> lines = {"abcabc"};
  expectPos(simulateMotionsDefault({0, 0}, "fa", lines), 0, 3, "fa when on 'a' finds next 'a'");
  expectPos(simulateMotionsDefault({0, 3}, "Fa", lines), 0, 0, "Fa when on 'a' finds previous 'a'");
}

// =============================================================================
// 11. SCROLL MOTIONS (<C-d>, <C-u>, <C-f>, <C-b>)
// =============================================================================

// Helper to simulate with custom NavContext
static Position simulateWithNav(Position start, const string& motion,
                                const vector<string>& lines, NavContext nav) {
  return simulateMotions(start, Mode::Normal, nav, motion, lines).pos;
}

// Generate a file with N lines for scroll testing
static vector<string> makeLines(int count) {
  vector<string> lines;
  for (int i = 0; i < count; i++) {
    lines.push_back("line" + to_string(i));
  }
  return lines;
}

// --- <C-d> Scroll Down Half Page ---

TEST_F(MotionTest, CtrlD_Basic) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);  // scrollAmount = 20

  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav), 20, 0, "C-d from line 0");
  expectPos(simulateWithNav({10, 0}, "<C-d>", lines, nav), 30, 0, "C-d from line 10");
  expectPos(simulateWithNav({50, 0}, "<C-d>", lines, nav), 70, 0, "C-d from line 50");
}

TEST_F(MotionTest, CtrlD_DifferentScrollAmounts) {
  auto lines = makeLines(100);

  NavContext nav10(0, 19, 20, 10);
  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav10), 10, 0, "C-d with scroll=10");

  NavContext nav5(0, 9, 10, 5);
  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav5), 5, 0, "C-d with scroll=5");

  NavContext nav30(0, 59, 60, 30);
  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav30), 30, 0, "C-d with scroll=30");
}

TEST_F(MotionTest, CtrlD_StopsAtEndOfFile) {
  auto lines = makeLines(50);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({40, 0}, "<C-d>", lines, nav), 49, 0, "C-d near end clamps to last line");
  expectPos(simulateWithNav({49, 0}, "<C-d>", lines, nav), 49, 0, "C-d at last line stays");
  expectPos(simulateWithNav({45, 0}, "<C-d>", lines, nav), 49, 0, "C-d partial scroll at end");
}

TEST_F(MotionTest, CtrlD_PreservesColumn) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({0, 3}, "<C-d>", lines, nav), 20, 3, "C-d preserves column");
}

TEST_F(MotionTest, CtrlD_ClampsColumnOnShorterLine) {
  vector<string> lines = {
    "long line here",  // 0
    "short",           // 1 (len 5)
    "long line here",  // 2
  };
  NavContext nav(0, 2, 3, 1);  // scroll 1 line at a time

  Position result = simulateWithNav({0, 10}, "<C-d>", lines, nav);
  EXPECT_EQ(result.line, 1);
  EXPECT_EQ(result.col, 4) << "Column should clamp to end of shorter line";
}

// --- <C-u> Scroll Up Half Page ---

TEST_F(MotionTest, CtrlU_Basic) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({50, 0}, "<C-u>", lines, nav), 30, 0, "C-u from line 50");
  expectPos(simulateWithNav({30, 0}, "<C-u>", lines, nav), 10, 0, "C-u from line 30");
  expectPos(simulateWithNav({99, 0}, "<C-u>", lines, nav), 79, 0, "C-u from last line");
}

TEST_F(MotionTest, CtrlU_DifferentScrollAmounts) {
  auto lines = makeLines(100);

  NavContext nav10(0, 19, 20, 10);
  expectPos(simulateWithNav({50, 0}, "<C-u>", lines, nav10), 40, 0, "C-u with scroll=10");

  NavContext nav15(0, 29, 30, 15);
  expectPos(simulateWithNav({50, 0}, "<C-u>", lines, nav15), 35, 0, "C-u with scroll=15");
}

TEST_F(MotionTest, CtrlU_StopsAtTopOfFile) {
  auto lines = makeLines(50);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({10, 0}, "<C-u>", lines, nav), 0, 0, "C-u near top clamps to line 0");
  expectPos(simulateWithNav({0, 0}, "<C-u>", lines, nav), 0, 0, "C-u at line 0 stays");
  expectPos(simulateWithNav({5, 0}, "<C-u>", lines, nav), 0, 0, "C-u partial scroll at top");
}

TEST_F(MotionTest, CtrlU_PreservesColumn) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({50, 3}, "<C-u>", lines, nav), 30, 3, "C-u preserves column");
}

// --- <C-f> Scroll Down Full Page ---

TEST_F(MotionTest, CtrlF_Basic) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);  // windowHeight = 40, so C-f moves 38 lines

  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav), 38, 0, "C-f from line 0");
  expectPos(simulateWithNav({10, 0}, "<C-f>", lines, nav), 48, 0, "C-f from line 10");
}

TEST_F(MotionTest, CtrlF_DifferentWindowHeights) {
  auto lines = makeLines(100);

  NavContext nav20(0, 19, 20, 10);  // windowHeight = 20, moves 18
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav20), 18, 0, "C-f with window=20");

  NavContext nav50(0, 49, 50, 25);  // windowHeight = 50, moves 48
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav50), 48, 0, "C-f with window=50");
}

TEST_F(MotionTest, CtrlF_StopsAtEndOfFile) {
  auto lines = makeLines(50);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({20, 0}, "<C-f>", lines, nav), 49, 0, "C-f near end clamps");
  expectPos(simulateWithNav({49, 0}, "<C-f>", lines, nav), 49, 0, "C-f at last line stays");
}

// --- <C-b> Scroll Up Full Page ---

TEST_F(MotionTest, CtrlB_Basic) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);  // windowHeight = 40, so C-b moves 38 lines

  expectPos(simulateWithNav({50, 0}, "<C-b>", lines, nav), 12, 0, "C-b from line 50");
  expectPos(simulateWithNav({99, 0}, "<C-b>", lines, nav), 61, 0, "C-b from last line");
}

TEST_F(MotionTest, CtrlB_DifferentWindowHeights) {
  auto lines = makeLines(100);

  NavContext nav20(0, 19, 20, 10);  // windowHeight = 20, moves 18
  expectPos(simulateWithNav({50, 0}, "<C-b>", lines, nav20), 32, 0, "C-b with window=20");

  NavContext nav50(0, 49, 50, 25);  // windowHeight = 50, moves 48
  expectPos(simulateWithNav({50, 0}, "<C-b>", lines, nav50), 2, 0, "C-b with window=50");
}

TEST_F(MotionTest, CtrlB_StopsAtTopOfFile) {
  auto lines = makeLines(50);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({20, 0}, "<C-b>", lines, nav), 0, 0, "C-b near top clamps");
  expectPos(simulateWithNav({0, 0}, "<C-b>", lines, nav), 0, 0, "C-b at line 0 stays");
}

// --- Combined Scroll Motions ---

TEST_F(MotionTest, Scroll_RoundTrip) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);

  // C-d then C-u should return to original line
  Position p1 = simulateWithNav({30, 0}, "<C-d>", lines, nav);
  Position p2 = simulateWithNav(p1, "<C-u>", lines, nav);
  EXPECT_EQ(p2.line, 30) << "C-d then C-u should return to original";

  // C-f then C-b should return to original line
  Position p3 = simulateWithNav({30, 0}, "<C-f>", lines, nav);
  Position p4 = simulateWithNav(p3, "<C-b>", lines, nav);
  EXPECT_EQ(p4.line, 30) << "C-f then C-b should return to original";
}

TEST_F(MotionTest, Scroll_MultipleScrolls) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 10);  // scrollAmount = 10

  // Multiple C-d
  Position p = {0, 0};
  p = simulateWithNav(p, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 10);
  p = simulateWithNav(p, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 20);
  p = simulateWithNav(p, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 30);
}

TEST_F(MotionTest, Scroll_CombinedWithOtherMotions) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);

  // C-d then j
  Position p1 = simulateWithNav({0, 0}, "<C-d>", lines, nav);
  Position p2 = simulateMotions(p1, Mode::Normal, nav, "j", lines).pos;
  EXPECT_EQ(p2.line, 21) << "C-d then j";

  // gg after C-d should go to line 0
  Position p3 = simulateWithNav({50, 0}, "<C-d>", lines, nav);
  Position p4 = simulateMotions(p3, Mode::Normal, nav, "gg", lines).pos;
  EXPECT_EQ(p4.line, 0) << "gg after C-d";
}

// --- Edge Cases ---

TEST_F(MotionTest, Scroll_SmallFile) {
  auto lines = makeLines(5);  // Only 5 lines
  NavContext nav(0, 39, 40, 20);  // scrollAmount > file size

  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav), 4, 0, "C-d in small file");
  expectPos(simulateWithNav({4, 0}, "<C-u>", lines, nav), 0, 0, "C-u in small file");
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav), 4, 0, "C-f in small file");
  expectPos(simulateWithNav({4, 0}, "<C-b>", lines, nav), 0, 0, "C-b in small file");
}

TEST_F(MotionTest, Scroll_SingleLine) {
  vector<string> lines = {"only line"};
  // Use realistic window size even for single line file
  NavContext nav(0, 0, 40, 20);

  expectPos(simulateWithNav({0, 0}, "<C-d>", lines, nav), 0, 0, "C-d single line");
  expectPos(simulateWithNav({0, 0}, "<C-u>", lines, nav), 0, 0, "C-u single line");
  expectPos(simulateWithNav({0, 0}, "<C-f>", lines, nav), 0, 0, "C-f single line");
  expectPos(simulateWithNav({0, 0}, "<C-b>", lines, nav), 0, 0, "C-b single line");
}

TEST_F(MotionTest, Scroll_EmptyLinesInFile) {
  vector<string> lines = {
    "content",
    "",
    "",
    "more content",
    "",
  };
  NavContext nav(0, 4, 5, 2);

  // Scroll should work normally with empty lines
  expectPos(simulateWithNav({0, 5}, "<C-d>", lines, nav), 2, 0, "C-d lands on empty line, col clamps");
  expectPos(simulateWithNav({4, 0}, "<C-u>", lines, nav), 2, 0, "C-u from empty line");
}

TEST_F(MotionTest, Scroll_ZeroScrollAmount) {
  // Edge case: what if scrollAmount is 0?
  auto lines = makeLines(50);
  NavContext nav(0, 39, 40, 0);

  // Should not move (or at minimum not crash)
  Position p = simulateWithNav({25, 0}, "<C-d>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-d with scroll=0 should not move";

  p = simulateWithNav({25, 0}, "<C-u>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-u with scroll=0 should not move";
}

TEST_F(MotionTest, Scroll_WindowHeightTwo) {
  // windowHeight = 2 means C-f/C-b move 0 lines (2-2=0)
  auto lines = makeLines(50);
  NavContext nav(0, 1, 2, 1);

  Position p = simulateWithNav({25, 0}, "<C-f>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-f with window=2 should not move";

  p = simulateWithNav({25, 0}, "<C-b>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-b with window=2 should not move";
}

TEST_F(MotionTest, Scroll_WindowHeightOne) {
  // windowHeight = 1 would compute negative jump without max(0, ...)
  // This tests the defensive guard against windowHeight < 2
  auto lines = makeLines(50);
  NavContext nav(0, 0, 1, 1);

  // Should not crash or move (jump = max(0, 1-2) = 0)
  Position p = simulateWithNav({25, 0}, "<C-f>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-f with window=1 should not move";

  p = simulateWithNav({25, 0}, "<C-b>", lines, nav);
  EXPECT_EQ(p.line, 25) << "C-b with window=1 should not move";
}

// =============================================================================
// 12. COUNT PREFIXES
// =============================================================================

// --- Basic count with h/l/j/k ---

TEST_F(MotionTest, Count_BasicHJKL) {
  expectPos(simulateMotionsDefault({0, 10}, "3h", a1_long_line), 0, 7, "3h moves left 3");
  expectPos(simulateMotionsDefault({0, 0}, "5l", a1_long_line), 0, 5, "5l moves right 5");
  expectPos(simulateMotionsDefault({0, 0}, "2j", a2_block_lines), 2, 0, "2j moves down 2");
  expectPos(simulateMotionsDefault({3, 0}, "2k", a2_block_lines), 1, 0, "2k moves up 2");
}

TEST_F(MotionTest, Count_LargeCount) {
  auto lines = makeLines(100);
  expectPos(simulateMotionsDefault({0, 0}, "50j", lines), 50, 0, "50j");
  expectPos(simulateMotionsDefault({99, 0}, "50k", lines), 49, 0, "50k");
  expectPos(simulateMotionsDefault({0, 0}, "20l", a1_long_line), 0, 20, "20l");
}

TEST_F(MotionTest, Count_ClampsAtBoundary) {
  expectPos(simulateMotionsDefault({0, 0}, "100j", a2_block_lines), 3, 0, "100j clamps to last line");
  expectPos(simulateMotionsDefault({3, 0}, "100k", a2_block_lines), 0, 0, "100k clamps to first line");
  expectPos(simulateMotionsDefault({0, 0}, "100l", a1_long_line), 0, 20, "100l clamps to line end");
  expectPos(simulateMotionsDefault({0, 10}, "100h", a1_long_line), 0, 0, "100h clamps to col 0");
}

// --- Count with $ ---

TEST_F(MotionTest, Count_Dollar) {
  // {count}$ moves down count-1 lines, then to end of line
  expectPos(simulateMotionsDefault({0, 0}, "1$", a2_block_lines), 0, 13, "1$ stays on same line, goes to end");
  expectPos(simulateMotionsDefault({0, 0}, "2$", a2_block_lines), 1, 13, "2$ goes to end of next line");
  expectPos(simulateMotionsDefault({0, 0}, "3$", a2_block_lines), 2, 13, "3$ goes to end of line 2 down");
}

// --- Count with gg and G ---

TEST_F(MotionTest, Count_ggG) {
  auto lines = makeLines(100);
  // gg with count goes to that line (1-indexed)
  expectPos(simulateMotionsDefault({50, 0}, "1gg", lines), 0, 0, "1gg goes to line 0");
  expectPos(simulateMotionsDefault({0, 0}, "10gg", lines), 9, 0, "10gg goes to line 9");
  expectPos(simulateMotionsDefault({0, 0}, "50gg", lines), 49, 0, "50gg goes to line 49");

  // G with count goes to that line (1-indexed)
  expectPos(simulateMotionsDefault({50, 0}, "1G", lines), 0, 0, "1G goes to line 0");
  expectPos(simulateMotionsDefault({0, 0}, "100G", lines), 99, 0, "100G goes to last line");
  expectPos(simulateMotionsDefault({0, 0}, "200G", lines), 99, 0, "200G clamps to last line");
}

// --- Count with word motions ---

TEST_F(MotionTest, Count_WordMotions) {
  // a1_long_line: "aaaaaa aaa aaaaaa aaa" (4 words)
  expectPos(simulateMotionsDefault({0, 0}, "2w", a1_long_line), 0, 11, "2w skips 2 words");
  expectPos(simulateMotionsDefault({0, 0}, "3w", a1_long_line), 0, 18, "3w skips 3 words");
  expectPos(simulateMotionsDefault({0, 18}, "2b", a1_long_line), 0, 7, "2b goes back 2 words");
}

// --- Count with f/F/t/T ---

TEST_F(MotionTest, Count_CharFind) {
  vector<string> lines = {"abcabcabc"};  // 'a' at 0, 3, 6
  // From col 0 (on 'a'), 1st 'a' after = col 3, 2nd 'a' after = col 6
  expectPos(simulateMotionsDefault({0, 0}, "2fa", lines), 0, 6, "2fa finds 2nd 'a' after cursor");
  expectPos(simulateMotionsDefault({0, 0}, "1fa", lines), 0, 3, "1fa finds 1st 'a' after cursor");
  // From col 8, going backward: 1st 'a' at 6, 2nd 'a' at 3
  expectPos(simulateMotionsDefault({0, 8}, "2Fa", lines), 0, 3, "2Fa finds 2nd 'a' backward");

  // Count with t - note: current impl applies till offset per-iteration
  // which may differ from Vim's exact behavior for counts > 1
  expectPos(simulateMotionsDefault({0, 0}, "ta", lines), 0, 2, "ta lands before 1st 'a'");
}

TEST_F(MotionTest, Count_CharFindWithRepeat) {
  vector<string> lines = {"abababab"};  // 'a' at 0, 2, 4, 6
  // From pos 0 (on 'a'): 1st 'a' after = 2, 2nd 'a' after = 4
  // 2fa lands on pos 4, then ; finds next 'a' at pos 6
  expectPos(simulateMotionsDefault({0, 0}, "2fa;", lines), 0, 6, "2fa; finds 2nd a then next");
  // After 2fa;, cursor at 6, ; would find nothing (no 'a' after 6)
  expectPos(simulateMotionsDefault({0, 0}, "fa;", lines), 0, 4, "fa; finds 1st a then next");
  expectPos(simulateMotionsDefault({0, 0}, "fa;;", lines), 0, 6, "fa;; finds 1st a then 2 more");
}

// --- Count with scroll commands ---

TEST_F(MotionTest, Count_CtrlD_SetsScrollAmount) {
  // In Vim, {count}<C-d> uses count as scroll amount (not repeat)
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);  // default scroll = 20

  // 5<C-d> should move 5 lines, not 5*20=100 lines
  expectPos(simulateWithNav({0, 0}, "5<C-d>", lines, nav), 5, 0, "5<C-d> moves 5 lines");
  expectPos(simulateWithNav({0, 0}, "10<C-d>", lines, nav), 10, 0, "10<C-d> moves 10 lines");
}

TEST_F(MotionTest, Count_CtrlU_SetsScrollAmount) {
  auto lines = makeLines(100);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({50, 0}, "5<C-u>", lines, nav), 45, 0, "5<C-u> moves up 5 lines");
  expectPos(simulateWithNav({50, 0}, "10<C-u>", lines, nav), 40, 0, "10<C-u> moves up 10 lines");
}

TEST_F(MotionTest, Count_CtrlF_RepeatsPages) {
  // In Vim, {count}<C-f> scrolls count pages (not sets amount)
  auto lines = makeLines(200);
  NavContext nav(0, 39, 40, 20);  // C-f moves windowHeight-2 = 38 lines per page

  expectPos(simulateWithNav({0, 0}, "2<C-f>", lines, nav), 76, 0, "2<C-f> moves 2 pages (76 lines)");
  expectPos(simulateWithNav({0, 0}, "3<C-f>", lines, nav), 114, 0, "3<C-f> moves 3 pages (114 lines)");
}

TEST_F(MotionTest, Count_CtrlB_RepeatsPages) {
  auto lines = makeLines(200);
  NavContext nav(0, 39, 40, 20);

  expectPos(simulateWithNav({100, 0}, "2<C-b>", lines, nav), 24, 0, "2<C-b> moves back 2 pages");
}

// --- Count with paragraph/sentence motions ---

TEST_F(MotionTest, Count_ParagraphMotions) {
  // a3_spaced_lines has blank lines creating paragraphs
  Position result = simulateMotionsDefault({0, 0}, "2}", a3_spaced_lines);
  EXPECT_GT(result.line, 0) << "2} should move forward";

  // Verify } called twice vs 2} gives same result
  Position p1 = simulateMotionsDefault({0, 0}, "}}", a3_spaced_lines);
  Position p2 = simulateMotionsDefault({0, 0}, "2}", a3_spaced_lines);
  expectPos(p2, p1.line, p1.col, "2} == }}");
}

// --- Edge cases for count parsing ---

TEST_F(MotionTest, Count_ZeroIsMotion) {
  // '0' is a motion to go to column 0, not a count prefix
  expectPos(simulateMotionsDefault({0, 10}, "0", a1_long_line), 0, 0, "0 goes to column 0");
  // Use multi-line file for 0j test
  expectPos(simulateMotionsDefault({0, 10}, "0j", a2_block_lines), 1, 0, "0j means 0 then j");
}

TEST_F(MotionTest, Count_LeadingZerosNotAllowed) {
  // "03j" should be: 0 (col 0), then 3j (down 3)
  expectPos(simulateMotionsDefault({0, 10}, "03j", a2_block_lines), 3, 0, "03j = 0 then 3j");
}

TEST_F(MotionTest, Count_MultiDigit) {
  auto lines = makeLines(150);
  expectPos(simulateMotionsDefault({0, 0}, "123j", lines), 123, 0, "123j moves down 123");
  expectPos(simulateMotionsDefault({149, 0}, "99k", lines), 50, 0, "99k moves up 99");
}

TEST_F(MotionTest, Count_NoCount) {
  // Without count, effectiveCount() should be 1
  expectPos(simulateMotionsDefault({0, 0}, "j", a2_block_lines), 1, 0, "j without count moves 1 line");
  expectPos(simulateMotionsDefault({0, 0}, "l", a1_long_line), 0, 1, "l without count moves 1 col");
}
