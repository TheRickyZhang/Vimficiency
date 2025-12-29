#include <gtest/gtest.h>
#include "TestUtils.h"
#include "Editor/Motion.h"

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

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
    m2_main_big = TestFiles::load("m2_main_big.txt");
  }

  // Helper: apply motion and return resulting position
  static Position runMotion(Position start, const string& motion, const vector<string>& lines) {
    return applyMotions(start, Mode::Normal, motion, lines).pos;
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
  expectPos(runMotion({0, 5}, "h", a1_long_line), 0, 4);
  expectPos(runMotion({0, 5}, "hhh", a1_long_line), 0, 2);
}

TEST_F(MotionTest, H_StopsAtLineStart) {
  expectPos(runMotion({0, 0}, "h", a1_long_line), 0, 0);
  expectPos(runMotion({0, 2}, "hhhhh", a1_long_line), 0, 0);
}

TEST_F(MotionTest, L_MovesRight) {
  expectPos(runMotion({0, 0}, "l", a1_long_line), 0, 1);
  expectPos(runMotion({0, 0}, "lll", a1_long_line), 0, 3);
}

TEST_F(MotionTest, L_StopsAtLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(runMotion({0, lastCol}, "l", a1_long_line), 0, lastCol);
}

// a2_block_lines: 4 identical lines of "aaaaaaaaaaaaaa"
TEST_F(MotionTest, J_MovesDown) {
  expectPos(runMotion({0, 0}, "j", a2_block_lines), 1, 0);
  expectPos(runMotion({0, 0}, "jj", a2_block_lines), 2, 0);
  expectPos(runMotion({0, 0}, "jjj", a2_block_lines), 3, 0);
}

TEST_F(MotionTest, J_StopsAtLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  expectPos(runMotion({lastLine, 0}, "j", a2_block_lines), lastLine, 0);
  expectPos(runMotion({0, 0}, "jjjjjjjj", a2_block_lines), lastLine, 0);
}

TEST_F(MotionTest, K_MovesUp) {
  expectPos(runMotion({2, 0}, "k", a2_block_lines), 1, 0);
  expectPos(runMotion({3, 0}, "kk", a2_block_lines), 1, 0);
}

TEST_F(MotionTest, K_StopsAtFirstLine) {
  expectPos(runMotion({0, 0}, "k", a2_block_lines), 0, 0);
  expectPos(runMotion({2, 0}, "kkkkk", a2_block_lines), 0, 0);
}

TEST_F(MotionTest, JK_PreservesColumn) {
  expectPos(runMotion({0, 5}, "j", a2_block_lines), 1, 5);
  expectPos(runMotion({0, 5}, "jj", a2_block_lines), 2, 5);
  expectPos(runMotion({2, 5}, "k", a2_block_lines), 1, 5);
}

TEST_F(MotionTest, JK_ClampsToShorterLine) {
  vector<string> lines = {"long line here", "short", "long line here"};
  expectPos(runMotion({0, 10}, "j", lines), 1, 4);
  expectPos(runMotion({0, 10}, "jk", lines), 0, 10);
}

TEST_F(MotionTest, JK_HandlesEmptyLines) {
  vector<string> lines = {"content", "", "content"};
  expectPos(runMotion({0, 5}, "j", lines), 1, 0);
  expectPos(runMotion({0, 5}, "jk", lines), 0, 5);
  expectPos(runMotion({1, 0}, "k", lines), 0, 0);
}

// =============================================================================
// 2. WORD MOTIONS (w, W, b, B, e, E)
// =============================================================================

// a1_long_line: "aaaaaa aaa aaaaaa aaa"
//                0      7   11     18
TEST_F(MotionTest, W_SmallWord_NextWordStart) {
  expectPos(runMotion({0, 0}, "w", a1_long_line), 0, 7);   // aaaaaa -> aaa
  expectPos(runMotion({0, 7}, "w", a1_long_line), 0, 11);  // aaa -> aaaaaa
  expectPos(runMotion({0, 11}, "w", a1_long_line), 0, 18); // aaaaaa -> aaa
}

TEST_F(MotionTest, W_MultipleWords) {
  expectPos(runMotion({0, 0}, "ww", a1_long_line), 0, 11);
  expectPos(runMotion({0, 0}, "www", a1_long_line), 0, 18);
}

TEST_F(MotionTest, B_SmallWord_PrevWordStart) {
  //                       0   4   8
  vector<string> lines = {"one two three"};
  expectPos(runMotion({0, 8}, "b", lines), 0, 4);   // three -> two
  expectPos(runMotion({0, 4}, "b", lines), 0, 0);   // two -> one
  expectPos(runMotion({0, 0}, "b", lines), 0, 0);   // one -> stays
}

TEST_F(MotionTest, E_SmallWord_WordEnd) {
  expectPos(runMotion({0, 0}, "e", a1_long_line), 0, 5);   // end of "aaaaaa"
  expectPos(runMotion({0, 5}, "e", a1_long_line), 0, 9);   // end of "aaa"
}

// m2_main_big has punctuation for testing W vs w
TEST_F(MotionTest, W_BigWord_SkipsPunctuation) {
  // Line 0: "#include <bits/stdc++.h>"
  // W should skip the whole #include or <bits/stdc++.h>
  Position p = runMotion({0, 0}, "W", m2_main_big);
  EXPECT_EQ(p.line, 0);
  EXPECT_GT(p.col, 1);  // should skip past #include
}

TEST_F(MotionTest, W_CrossesLines) {
  // From end of a1_long_line (single line), w should stay
  int lastWord = 18;
  Position p = runMotion({0, lastWord}, "w", a1_long_line);
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
    p = runMotion(p, "w", m2_main_big);
    jumps++;
  }
  EXPECT_EQ(p.line, 1) << "w should eventually cross to line 1";
}

TEST_F(MotionTest, WordMotionFromEmptyLine) {
  vector<string> lines = {"", "content"};
  // w from empty line should go to next line's first word
  Position p = runMotion({0, 0}, "w", lines);
  EXPECT_EQ(p.line, 1);
  EXPECT_EQ(p.col, 0);
}

// =============================================================================
// 3. LINE MOTIONS (0, ^, $)
// =============================================================================

TEST_F(MotionTest, Zero_GoesToColumnZero) {
  expectPos(runMotion({0, 10}, "0", a1_long_line), 0, 0);
  expectPos(runMotion({0, 5}, "0", a2_block_lines), 0, 0);
}

TEST_F(MotionTest, Dollar_GoesToLineEnd) {
  int lastCol = a1_long_line[0].size() - 1;
  expectPos(runMotion({0, 0}, "$", a1_long_line), 0, lastCol);
  expectPos(runMotion({0, 5}, "$", a1_long_line), 0, lastCol);
}

TEST_F(MotionTest, Dollar_OnEmptyLine) {
  vector<string> lines = {"", "content"};
  expectPos(runMotion({0, 0}, "$", lines), 0, 0);
}

TEST_F(MotionTest, Caret_GoesToFirstNonBlank) {
  vector<string> lines = {"  indented"};
  Position p = runMotion({0, 5}, "^", lines);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, 2);  // first non-blank after 2 spaces
}

TEST_F(MotionTest, Caret_OnNoIndent) {
  vector<string> lines = {"noindent"};
  Position p = runMotion({0, 5}, "^", lines);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, 0);
}

// =============================================================================
// 4. PARAGRAPH/SENTENCE MOTIONS ({, }, (, ))
// =============================================================================

TEST_F(MotionTest, CloseBrace_NextParagraph) {
  vector<string> lines = {"para1", "para1", "", "para2", "para2"};
  Position p = runMotion({0, 0}, "}", lines);
  EXPECT_EQ(p.line, 2);  // blank line
}

TEST_F(MotionTest, OpenBrace_PrevParagraph) {
  vector<string> lines = {"para1", "", "para2", "para2"};
  Position p = runMotion({3, 0}, "{", lines);
  EXPECT_EQ(p.line, 1);  // blank line
}

TEST_F(MotionTest, MultipleBraceJumps) {
  vector<string> lines = {"a", "", "b", "", "c"};
  Position p1 = runMotion({0, 0}, "}", lines);
  Position p2 = runMotion({0, 0}, "}}", lines);
  EXPECT_EQ(p1.line, 1);
  EXPECT_EQ(p2.line, 3);
}

TEST_F(MotionTest, CloseParen_NextSentence) {
  vector<string> lines = {"First. Second."};
  Position start(0, 0);
  Position p = runMotion(start, ")", lines);
  // Should move to "Second" (after ". ")
  EXPECT_GT(p.col, 0) << ") should move cursor forward";
}

TEST_F(MotionTest, OpenParen_PrevSentence) {
  vector<string> lines = {"First. Second."};
  Position start(0, 10);
  Position p = runMotion(start, "(", lines);
  EXPECT_LT(p.col, start.col) << "( should move cursor backward";
}

// =============================================================================
// 5. FILE MOTIONS (gg, G)
// =============================================================================

// Neovim has nostartofline by default, so preserve column!
TEST_F(MotionTest, GG_GoesToFirstLine) {
  Position p1 = runMotion({3, 5}, "gg", a2_block_lines);
  EXPECT_EQ(p1.line, 0);
  EXPECT_EQ(p1.col, 5);
  
  vector<string> lines = {"short", "longer line"};
  Position p2 = runMotion({1, 8}, "gg", lines);
  EXPECT_EQ(p2.line, 0);
  EXPECT_EQ(p2.col, 4);
}

TEST_F(MotionTest, G_GoesToLastLine) {
  int lastLine = a2_block_lines.size() - 1;
  Position p = runMotion({0, 5}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
  EXPECT_EQ(p.col, 5);
}

TEST_F(MotionTest, GG_G_RoundTrip) {
  Position start(2, 5);
  Position atTop = runMotion(start, "gg", a2_block_lines);
  Position atBottom = runMotion(atTop, "G", a2_block_lines);
  Position backToTop = runMotion(atBottom, "gg", a2_block_lines);
  
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
  expectPos(runMotion({0, 0}, "jjl", a2_block_lines), 2, 1);
  
  // kkhh - up 2, left 2
  expectPos(runMotion({2, 5}, "kkhh", a2_block_lines), 0, 3);
}

TEST_F(MotionTest, CombinedWordLine) {
  // w$ - next word, then end of line
  Position p = runMotion({0, 0}, "w$", a1_long_line);
  EXPECT_EQ(p.line, 0);
  EXPECT_EQ(p.col, (int)a1_long_line[0].size() - 1);
}

TEST_F(MotionTest, NavigateCodeBlock) {
  // Navigate through m2_main_big
  // Start at line 0, go to main() line
  Position p = runMotion({0, 0}, "jj", m2_main_big);  // line 2: "int main() {"
  EXPECT_EQ(p.line, 2);
}

// =============================================================================
// 7. EDGE CASES
// =============================================================================

TEST_F(MotionTest, EmptyLineNavigation) {
  vector<string> lines = {""};
  expectPos(runMotion({0, 0}, "l", lines), 0, 0);  // can't move right on empty
  expectPos(runMotion({0, 0}, "h", lines), 0, 0);  // can't move left on empty
  expectPos(runMotion({0, 0}, "$", lines), 0, 0);  // $ on empty stays
  expectPos(runMotion({0, 0}, "0", lines), 0, 0);  // 0 on empty stays
}

TEST_F(MotionTest, SingleCharLine) {
  vector<string> lines = {"a"};
  expectPos(runMotion({0, 0}, "l", lines), 0, 0);
  expectPos(runMotion({0, 0}, "h", lines), 0, 0);
  expectPos(runMotion({0, 0}, "$", lines), 0, 0);
  expectPos(runMotion({0, 0}, "0", lines), 0, 0);
}

TEST_F(MotionTest, FirstAndLastPositions) {
  // gg from (0,0) stays at line 0
  EXPECT_EQ(runMotion({0, 0}, "gg", a2_block_lines).line, 0);
  
  // G from last line stays at last line
  int lastLine = a2_block_lines.size() - 1;
  Position p = runMotion({lastLine, 0}, "G", a2_block_lines);
  EXPECT_EQ(p.line, lastLine);
}

// =============================================================================
// 8. PROPERTY-BASED TESTS (invariants that must always hold)
// =============================================================================

TEST_F(MotionTest, Property_H_NeverIncreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = runMotion({0, col}, "h", a1_long_line);
    EXPECT_LE(result.col, col) << "h from col " << col << " should not increase col";
    EXPECT_EQ(result.line, 0) << "h should not change line";
  }
}

TEST_F(MotionTest, Property_L_NeverDecreasesColumn) {
  for(int col = 0; col < 10; col++) {
    Position result = runMotion({0, col}, "l", a1_long_line);
    EXPECT_GE(result.col, col) << "l from col " << col << " should not decrease col";
    EXPECT_EQ(result.line, 0) << "l should not change line";
  }
}

TEST_F(MotionTest, Property_J_NeverDecreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = runMotion({line, 0}, "j", a2_block_lines);
    EXPECT_GE(result.line, line) << "j from line " << line << " should not decrease line";
  }
}

TEST_F(MotionTest, Property_K_NeverIncreasesLine) {
  for(int line = 0; line < (int)a2_block_lines.size(); line++) {
    Position result = runMotion({line, 0}, "k", a2_block_lines);
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
        Position result = runMotion({line, col}, motion, a2_block_lines);
        
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
    Position result = runMotion({line, 5}, "gg", m2_main_big);
    EXPECT_EQ(result.line, 0) << "gg from line " << line << " should go to line 0";
  }
}

TEST_F(MotionTest, Property_G_AlwaysLastLine) {
  int lastLine = m2_main_big.size() - 1;
  for(int line = 0; line < (int)m2_main_big.size(); line++) {
    Position result = runMotion({line, 0}, "G", m2_main_big);
    EXPECT_EQ(result.line, lastLine) << "G from line " << line << " should go to last line";
  }
}

TEST_F(MotionTest, Property_Zero_AlwaysColumn0) {
  for(int col = 0; col < 15; col++) {
    Position result = runMotion({0, col}, "0", a1_long_line);
    EXPECT_EQ(result.col, 0) << "0 from col " << col << " should go to col 0";
    EXPECT_EQ(result.line, 0) << "0 should not change line";
  }
}

// =============================================================================
// 9. REALISTIC NAVIGATION SCENARIOS
// =============================================================================

TEST_F(MotionTest, Scenario_JumpToEndAndBack) {
  Position start(3, 5);
  Position end = runMotion(start, "G", m2_main_big);
  Position back = runMotion(end, "gg", m2_main_big);
  
  EXPECT_EQ(end.line, (int)m2_main_big.size() - 1);
  EXPECT_EQ(back.line, 0);
}

TEST_F(MotionTest, Scenario_WordNavigationAcrossFile) {
  // Count how many w's to get from start to line 5
  Position p(0, 0);
  int count = 0;
  while(p.line < 5 && count < 50) {
    p = runMotion(p, "w", m2_main_big);
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
  expectPos(runMotion({0, 0}, "fc", lines), 0, 2, "f from 0 to 'c'");
  expectPos(runMotion({0, 0}, "fj", lines), 0, 9, "f from 0 to 'j'");
  expectPos(runMotion({0, 3}, "fg", lines), 0, 6, "f from 3 to 'g'");
}

TEST_F(MotionTest, F_FindForward_NotFound) {
  vector<string> lines = {"abcdefghij"};
  // Target not found - position should stay unchanged
  expectPos(runMotion({0, 0}, "fz", lines), 0, 0, "f to nonexistent char");
  expectPos(runMotion({0, 5}, "fa", lines), 0, 5, "f forward to char behind cursor");
}

TEST_F(MotionTest, F_FindBackward_Basic) {
  vector<string> lines = {"abcdefghij"};
  expectPos(runMotion({0, 9}, "Fc", lines), 0, 2, "F from 9 to 'c'");
  expectPos(runMotion({0, 9}, "Fa", lines), 0, 0, "F from 9 to 'a'");
  expectPos(runMotion({0, 6}, "Fd", lines), 0, 3, "F from 6 to 'd'");
}

TEST_F(MotionTest, F_FindBackward_NotFound) {
  vector<string> lines = {"abcdefghij"};
  expectPos(runMotion({0, 9}, "Fz", lines), 0, 9, "F to nonexistent char");
  expectPos(runMotion({0, 3}, "Fj", lines), 0, 3, "F backward to char ahead of cursor");
}

TEST_F(MotionTest, T_TillForward_Basic) {
  vector<string> lines = {"abcdefghij"};
  // t lands ONE BEFORE the target
  expectPos(runMotion({0, 0}, "tc", lines), 0, 1, "t from 0 to before 'c'");
  expectPos(runMotion({0, 0}, "tj", lines), 0, 8, "t from 0 to before 'j'");
  expectPos(runMotion({0, 3}, "tg", lines), 0, 5, "t from 3 to before 'g'");
}

TEST_F(MotionTest, T_TillForward_AdjacentChar) {
  vector<string> lines = {"abcdefghij"};
  // t to adjacent char - lands on current position (one before target)
  expectPos(runMotion({0, 0}, "tb", lines), 0, 0, "t to adjacent char stays");
}

TEST_F(MotionTest, T_TillBackward_Basic) {
  vector<string> lines = {"abcdefghij"};
  // T lands ONE AFTER the target (going backward)
  expectPos(runMotion({0, 9}, "Tc", lines), 0, 3, "T from 9 to after 'c'");
  expectPos(runMotion({0, 9}, "Ta", lines), 0, 1, "T from 9 to after 'a'");
}

TEST_F(MotionTest, T_TillBackward_AdjacentChar) {
  vector<string> lines = {"abcdefghij"};
  expectPos(runMotion({0, 5}, "Te", lines), 0, 5, "T to adjacent char stays");
}

TEST_F(MotionTest, CharFind_WithSemicolonRepeat) {
  vector<string> lines = {"abcabcabc"};  // 'a' at 0, 3, 6; 'b' at 1, 4, 7; 'c' at 2, 5, 8
  expectPos(runMotion({0, 0}, "fa", lines), 0, 3, "fa from 0");
  expectPos(runMotion({0, 0}, "fa;", lines), 0, 6, "fa; from 0");
  expectPos(runMotion({0, 8}, "Fa", lines), 0, 6, "Fa from 8");
  expectPos(runMotion({0, 8}, "Fa;", lines), 0, 3, "Fa; from 8");
  expectPos(runMotion({0, 8}, "Fa;;", lines), 0, 0, "Fa;; from 8");
}

TEST_F(MotionTest, CharFind_WithCommaRepeat) {
  vector<string> lines = {"abcabcabc"};
  expectPos(runMotion({0, 0}, "fa,", lines), 0, 0, "fa, from 0 - back to start");
  expectPos(runMotion({0, 0}, "fa;,", lines), 0, 3, "fa;, from 0 - forward twice, back once");
  expectPos(runMotion({0, 8}, "Fa,", lines), 0, 6, "Fa, from 8 - stays at 6 (no 'a' ahead of 6)");
}

TEST_F(MotionTest, CharFind_MixedRepeat) {
  vector<string> lines = {"abcabcabc"};
  // 0 -> 3 (fa) -> 6 (;) -> 3 (,) -> 6 (;)
  expectPos(runMotion({0, 0}, "fa;,;", lines), 0, 6, "fa;,; complex repeat");
}

TEST_F(MotionTest, CharFind_TillWithRepeat) {
  vector<string> lines = {"abcabcabc"};
  // ta - till 'a', lands one before
  expectPos(runMotion({0, 0}, "ta", lines), 0, 2, "ta from 0 - before first 'a' at 3");
  // This is correct - t; can get "stuck" when you're right before the target
  expectPos(runMotion({0, 0}, "ta;", lines), 0, 2, "ta; from 0 - stuck at 2 (next 'a' is at 3)");
  // fa; uses f behavior for repeats (landing on target), progressing each time
  expectPos(runMotion({0, 0}, "fa;", lines), 0, 6, "fa; from 0 advances properly");
}

TEST_F(MotionTest, CharFind_SpaceAsTarget) {
  vector<string> lines = {"abc def ghi"};  // spaces at 3, 7
  expectPos(runMotion({0, 0}, "f ", lines), 0, 3, "f<space> from 0");
  expectPos(runMotion({0, 0}, "f ;", lines), 0, 7, "f<space>; from 0");
  expectPos(runMotion({0, 0}, "t ", lines), 0, 2, "t<space> from 0");
  expectPos(runMotion({0, 10}, "F ", lines), 0, 7, "F<space> from end");
  expectPos(runMotion({0, 10}, "F ;", lines), 0, 3, "F<space>; from end");
}

TEST_F(MotionTest, CharFind_MultipleOccurrences) {
  vector<string> lines = {"aaaaaa"};
  expectPos(runMotion({0, 0}, "fa", lines), 0, 1, "fa in all-a line");
  expectPos(runMotion({0, 0}, "fa;", lines), 0, 2, "fa; in all-a line");
  expectPos(runMotion({0, 0}, "fa;;", lines), 0, 3, "fa;; in all-a line");
  expectPos(runMotion({0, 0}, "fa;;;", lines), 0, 4, "fa;;; in all-a line");
}

TEST_F(MotionTest, CharFind_CombinedWithOtherMotions) {
  vector<string> lines = {"abcdefghij", "0123456789"};
  expectPos(runMotion({0, 0}, "jfc", lines), 1, 0, "j then fc (no 'c' in line 1)");
  expectPos(runMotion({0, 0}, "fcj", lines), 1, 2, "fc then j");
}

TEST_F(MotionTest, CharFind_AtLineEnd) {
  vector<string> lines = {"abcdef"};
  expectPos(runMotion({0, 5}, "fa", lines), 0, 5, "f from end - target behind");
  expectPos(runMotion({0, 0}, "Ff", lines), 0, 0, "F from start - target ahead");
}

TEST_F(MotionTest, CharFind_OnTargetChar) {
  vector<string> lines = {"abcabc"};
  expectPos(runMotion({0, 0}, "fa", lines), 0, 3, "fa when on 'a' finds next 'a'");
  expectPos(runMotion({0, 3}, "Fa", lines), 0, 0, "Fa when on 'a' finds previous 'a'");
}
