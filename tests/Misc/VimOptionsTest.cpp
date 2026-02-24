// tests/Misc/VimOptionsTest.cpp
//
// Oracle-verified tests for behaviors that differ between Neovim and legacy Vim.
// Tests adapt expectations at compile time via VimOptions, so they pass in both
// VIMFICIENCY_LEGACY_VIM=ON and OFF configurations.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="VimOptionsTest.*"

#include <gtest/gtest.h>

#include "Interpreter/MotionInterpreter.h"
#include "Types/NavContext.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/Indentation.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimOptions.h"

using namespace std;

class VimOptionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
    navContext = NavContext(80, 39);
  }

  static void TearDownTestSuite() { oracle.reset(); }

  static SimulationResult oracleSimulate(const Lines& lines, int row, int col,
                                         const string& keys) {
    return oracle->simulate(lines, row, col, keys);
  }

  static CursorPos oracleMotion(const Lines& lines, int row, int col,
                               const string& keys) {
    auto result = oracle->simulate(lines, row, col, keys);
    return CursorPos(result.row, result.col);
  }
};

unique_ptr<NeovimOracle> VimOptionsTest::oracle;
NavContext VimOptionsTest::navContext(0, 0);

// =============================================================================
// startofline tests
// =============================================================================
// Legacy Vim (ON):  gg/G/dd go to first non-blank character
// Neovim (OFF):     gg/G/dd preserve column position

TEST_F(VimOptionsTest, GG_StartOfLine) {
  Lines lines = {"    hello", "world", "  foo"};
  // Cursor starts at col 5 on "world", then gg goes to first line
  auto result = oracleMotion(lines, 1, 4, "gg");
  CursorPos ours = simulateMotions({1, 4}, "gg", lines, navContext);

  EXPECT_EQ(ours.line, result.line);
  EXPECT_EQ(ours.col, result.col);

  if constexpr (VimOptions::startOfLine()) {
    // Legacy: first non-blank of "    hello" is col 4
    EXPECT_EQ(result.col, 4);
  } else {
    // Neovim: preserves col, clamped to line length-1
    EXPECT_EQ(result.col, 4);
  }
}

TEST_F(VimOptionsTest, G_StartOfLine) {
  Lines lines = {"    hello", "world", "  foo"};
  // Cursor at col 0 on first line, G goes to last line
  auto result = oracleMotion(lines, 0, 0, "G");
  CursorPos ours = simulateMotions({0, 0}, "G", lines, navContext);

  EXPECT_EQ(ours.line, result.line);
  EXPECT_EQ(ours.col, result.col);

  if constexpr (VimOptions::startOfLine()) {
    // Legacy: first non-blank of "  foo" is col 2
    EXPECT_EQ(result.col, 2);
  } else {
    // Neovim: preserves col 0
    EXPECT_EQ(result.col, 0);
  }
}

TEST_F(VimOptionsTest, GG_CursorBeyondLineLength) {
  Lines lines = {"ab", "          long line", "cd"};
  // Start at col 15 on long line, gg to short first line
  auto result = oracleMotion(lines, 1, 15, "gg");
  CursorPos ours = simulateMotions({1, 15}, "gg", lines, navContext);

  EXPECT_EQ(ours.line, result.line);
  EXPECT_EQ(ours.col, result.col);

  if constexpr (VimOptions::startOfLine()) {
    EXPECT_EQ(result.col, 0); // first non-blank of "ab"
  } else {
    EXPECT_EQ(result.col, 1); // clamped to last col of "ab"
  }
}

TEST_F(VimOptionsTest, DD_StartOfLine) {
  Lines lines = {"    hello", "  world", "foo"};
  // Delete first line, cursor should land on "  world" (now first line)
  auto result = oracleSimulate(lines, 0, 0, "dd");

  // Our implementation: deleteRangeLinewise handles startOfLine
  Lines ourLines = lines;
  CursorPos ourPos(0, 0);
  VimCore::deleteRangeLinewise(ourLines, LineRange(0, 1), ourPos);

  EXPECT_EQ(ourPos.line, result.row);
  EXPECT_EQ(ourPos.col, result.col);
  EXPECT_EQ(ourLines, result.lines);

  if constexpr (VimOptions::startOfLine()) {
    // Legacy: cursor at first non-blank of "  world" = col 2
    EXPECT_EQ(result.col, 2);
  } else {
    // Neovim: cursor at col 0 (preserves targetCol from before)
    EXPECT_EQ(result.col, 0);
  }
}

TEST_F(VimOptionsTest, DD_MiddleLine) {
  Lines lines = {"abc", "    indented", "  next"};
  // Delete middle line from col 6
  auto result = oracleSimulate(lines, 1, 6, "dd");

  Lines ourLines = lines;
  CursorPos ourPos(1, 6);
  VimCore::deleteRangeLinewise(ourLines, LineRange(1, 2), ourPos);

  EXPECT_EQ(ourPos.line, result.row);
  EXPECT_EQ(ourPos.col, result.col);
  EXPECT_EQ(ourLines, result.lines);
}

// =============================================================================
// joinspaces tests
// =============================================================================
// Legacy Vim (ON):  J adds 2 spaces after .!?
// Neovim (OFF):     J always adds 1 space

TEST_F(VimOptionsTest, J_AfterPeriod) {
  Lines lines = {"end.", "Next line"};
  auto result = oracleSimulate(lines, 0, 0, "J");

  Lines ourLines = lines;
  CursorPos ourPos(0, 0);
  VimCore::joinLines(ourLines, ourPos, true);

  EXPECT_EQ(ourLines, result.lines);
  EXPECT_EQ(ourPos.line, result.row);
  EXPECT_EQ(ourPos.col, result.col);

  if constexpr (VimOptions::joinSpaces()) {
    EXPECT_EQ(ourLines[0], "end.  Next line"); // 2 spaces
  } else {
    EXPECT_EQ(ourLines[0], "end. Next line"); // 1 space
  }
}

TEST_F(VimOptionsTest, J_AfterExclamation) {
  Lines lines = {"wow!", "more"};
  auto result = oracleSimulate(lines, 0, 0, "J");

  Lines ourLines = lines;
  CursorPos ourPos(0, 0);
  VimCore::joinLines(ourLines, ourPos, true);

  EXPECT_EQ(ourLines, result.lines);
  EXPECT_EQ(ourPos.line, result.row);
  EXPECT_EQ(ourPos.col, result.col);

  if constexpr (VimOptions::joinSpaces()) {
    EXPECT_EQ(ourLines[0], "wow!  more");
  } else {
    EXPECT_EQ(ourLines[0], "wow! more");
  }
}

TEST_F(VimOptionsTest, J_AfterQuestion) {
  Lines lines = {"really?", "yes"};
  auto result = oracleSimulate(lines, 0, 0, "J");

  Lines ourLines = lines;
  CursorPos ourPos(0, 0);
  VimCore::joinLines(ourLines, ourPos, true);

  EXPECT_EQ(ourLines, result.lines);
  EXPECT_EQ(ourPos.line, result.row);
  EXPECT_EQ(ourPos.col, result.col);

  if constexpr (VimOptions::joinSpaces()) {
    EXPECT_EQ(ourLines[0], "really?  yes");
  } else {
    EXPECT_EQ(ourLines[0], "really? yes");
  }
}

TEST_F(VimOptionsTest, J_AfterNormalChar_SingleSpaceBothModes) {
  Lines lines = {"hello", "world"};
  auto result = oracleSimulate(lines, 0, 0, "J");

  Lines ourLines = lines;
  CursorPos ourPos(0, 0);
  VimCore::joinLines(ourLines, ourPos, true);

  EXPECT_EQ(ourLines, result.lines);
  EXPECT_EQ(ourLines[0], "hello world"); // 1 space in both modes
}

TEST_F(VimOptionsTest, J_NextLineLeadingWhitespace) {
  Lines lines = {"first.", "   second"};
  auto result = oracleSimulate(lines, 0, 0, "J");

  Lines ourLines = lines;
  CursorPos ourPos(0, 0);
  VimCore::joinLines(ourLines, ourPos, true);

  EXPECT_EQ(ourLines, result.lines);
  EXPECT_EQ(ourPos.col, result.col);

  // Leading whitespace on next line is stripped before joining
  if constexpr (VimOptions::joinSpaces()) {
    EXPECT_EQ(ourLines[0], "first.  second");
  } else {
    EXPECT_EQ(ourLines[0], "first. second");
  }
}

// =============================================================================
// autoindent tests
// =============================================================================
// Neovim (ON):  cc/o/CR copy indent from current/previous line
// Legacy Vim (OFF): no automatic indentation

TEST_F(VimOptionsTest, O_AutoindentCopied) {
  Lines lines = {"    indented"};
  // o opens line below; with autoindent, new line gets same indent
  auto result = oracleSimulate(lines, 0, 4, "otext<Esc>");

  if constexpr (VimOptions::autoindent()) {
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[1], "    text");
  } else {
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[1], "text");
  }
}

TEST_F(VimOptionsTest, CC_AutoindentCopied) {
  Lines lines = {"    indented"};
  // cc clears line and enters insert mode; with autoindent, indent is preserved
  auto result = oracleSimulate(lines, 0, 6, "ccnew<Esc>");

  ASSERT_EQ(result.lines.size(), 1u);
  if constexpr (VimOptions::autoindent()) {
    EXPECT_EQ(result.lines[0], "    new");
  } else {
    EXPECT_EQ(result.lines[0], "new");
  }
}

TEST_F(VimOptionsTest, CR_AutoindentContinues) {
  Lines lines = {"    hello"};
  // A goes to end, <CR> creates new line
  auto result = oracleSimulate(lines, 0, 0, "A<CR>world<Esc>");

  ASSERT_EQ(result.lines.size(), 2u);
  EXPECT_EQ(result.lines[0], "    hello");
  if constexpr (VimOptions::autoindent()) {
    EXPECT_EQ(result.lines[1], "    world");
  } else {
    EXPECT_EQ(result.lines[1], "world");
  }
}

TEST_F(VimOptionsTest, BuildTypedCommands_MatchesOracle_CC) {
  // Verify that buildTypedCommands produces a sequence that, when replayed
  // in oracle after cc, yields the correct goal lines.
  Lines initialLines = {"        deep indent"};
  Lines goalLines = {"    less"};

  // Compute what our buildTypedCommands would produce
  string_view autoindent = VimOptions::autoindent()
      ? leadingWhitespace(initialLines[0])
      : string_view("");
  KeyedSequence typed = buildTypedCommands(goalLines, autoindent);

  // Replay in oracle: cc enters insert with autoindent, then our sequence
  string fullSeq = "cc" + typed.seq.str();
  auto result = oracleSimulate(initialLines, 0, 0, fullSeq);

  ASSERT_EQ(result.lines.size(), goalLines.size());
  for (size_t i = 0; i < goalLines.size(); i++) {
    EXPECT_EQ(result.lines[i], goalLines[i])
        << "Line " << i << " mismatch for typed sequence: " << typed.seq;
  }
}

TEST_F(VimOptionsTest, BuildTypedCommands_MultiLine) {
  Lines initialLines = {"    original"};
  Lines goalLines = {"    first", "    second", "third"};

  string_view autoindent = VimOptions::autoindent()
      ? leadingWhitespace(initialLines[0])
      : string_view("");
  KeyedSequence typed = buildTypedCommands(goalLines, autoindent);

  string fullSeq = "cc" + typed.seq.str();
  auto result = oracleSimulate(initialLines, 0, 0, fullSeq);

  ASSERT_EQ(result.lines.size(), goalLines.size());
  for (size_t i = 0; i < goalLines.size(); i++) {
    EXPECT_EQ(result.lines[i], goalLines[i])
        << "Line " << i << " mismatch for typed sequence: " << typed.seq;
  }
}
