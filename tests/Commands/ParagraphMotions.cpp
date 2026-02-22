// tests/Commands/ParagraphMotions.cpp
//
// Tests for paragraph motion commands (}, {) using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="ParagraphMotionsTest.*"

#include <gtest/gtest.h>

#include "Interpreter/MotionInterpreter.h"
#include "VimTypes/NavContext.h"
#include "VimTypes/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class ParagraphMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;
  static constexpr int NUM_ITERATIONS = 100;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
    navContext = NavContext();
  }

  static void TearDownTestSuite() { oracle.reset(); }

  // Get expected result from Neovim
  static Position neovimMotion(const Lines& lines, int startRow, int startCol,
                               const string& keys) {
    auto result = oracle->simulate(lines, startRow, startCol, keys);
    return Position(result.row, result.col);
  }
};

unique_ptr<NeovimOracle> ParagraphMotionsTest::oracle;
NavContext ParagraphMotionsTest::navContext(0, 0);

// =============================================================================
// Random Buffer Generation for Paragraph Testing
// =============================================================================
//
// Paragraphs are linewise, so we use coarser-grained generation:
// - Lines are either blank or have content
// - No need for character-level variation within lines
// - Focus on different paragraph structures

struct RandomParagraphTest {
  Lines lines;
  int cursorLine;
  int cursorCol;
};

// Generate a random buffer with paragraph structure
RandomParagraphTest generateRandomParagraphBuffer(int numLines) {
  RandomParagraphTest test;

  for (int i = 0; i < numLines; i++) {
    if (RandomGen::chance(1, 4)) {  // ~25% blank lines
      test.lines.push_back("");
    } else {
      // Content line
      int len = RandomGen::range(5, 30);
      string line;
      line.reserve(len);
      for (int j = 0; j < len; j++) {
        line += RandomGen::pick(CharPools::LETTERS);
      }
      test.lines.push_back(line);
    }
  }

  // Ensure at least one non-blank line
  bool hasContent = false;
  for (const auto& line : test.lines) {
    if (!line.empty()) {
      hasContent = true;
      break;
    }
  }
  if (!hasContent && !test.lines.empty()) {
    test.lines[0] = "content";
  }

  // Pick a random cursor position
  test.cursorLine = RandomGen::range(0, numLines - 1);

  int lineLen2 = test.lines[test.cursorLine].size();
  if (lineLen2 > 0) {
    test.cursorCol = RandomGen::range(0, lineLen2 - 1);
  } else {
    test.cursorCol = 0;
  }

  return test;
}

// =============================================================================
// Random Buffer Tests - } motion
// =============================================================================

TEST_F(ParagraphMotionsTest, ForwardParagraph_RandomBuffer) {
  RandomGen::seed(42);
  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int numLines = RandomGen::range(3, 15);
    auto test = generateRandomParagraphBuffer(numLines);

    Position start(test.cursorLine, test.cursorCol);
    Position ours = simulateMotions(start, "}", test.lines);
    Position expected = neovimMotion(test.lines, test.cursorLine, test.cursorCol, "}");

    if (ours.line == expected.line && ours.col == expected.col) {
      passed++;
    } else {
      // Debug output for failures
      ADD_FAILURE() << "Iteration " << i << ": } motion mismatch\n"
                    << "Buffer (" << test.lines.size() << " lines):\n"
                    << [&]() {
                         string s;
                         for (size_t j = 0; j < test.lines.size(); j++) {
                           s += "  [" + to_string(j) + "]: \"" + test.lines[j] +
                                "\"\n";
                         }
                         return s;
                       }()
                    << "Start: (" << test.cursorLine << ", " << test.cursorCol
                    << ")\n"
                    << "Ours: (" << ours.line << ", " << ours.col << ")\n"
                    << "Expected: (" << expected.line << ", " << expected.col
                    << ")";
    }
  }

  EXPECT_EQ(passed, NUM_ITERATIONS);
}

// =============================================================================
// Random Buffer Tests - { motion
// =============================================================================

TEST_F(ParagraphMotionsTest, BackwardParagraph_RandomBuffer) {
  RandomGen::seed(123);

  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int numLines = RandomGen::range(3, 15);
    auto test = generateRandomParagraphBuffer(numLines);

    Position start(test.cursorLine, test.cursorCol);
    Position ours = simulateMotions(start, "{", test.lines);
    Position expected = neovimMotion(test.lines, test.cursorLine, test.cursorCol, "{");

    if (ours.line == expected.line && ours.col == expected.col) {
      passed++;
    } else {
      ADD_FAILURE() << "Iteration " << i << ": { motion mismatch\n"
                    << "Buffer (" << test.lines.size() << " lines):\n"
                    << [&]() {
                         string s;
                         for (size_t j = 0; j < test.lines.size(); j++) {
                           s += "  [" + to_string(j) + "]: \"" + test.lines[j] +
                                "\"\n";
                         }
                         return s;
                       }()
                    << "Start: (" << test.cursorLine << ", " << test.cursorCol
                    << ")\n"
                    << "Ours: (" << ours.line << ", " << ours.col << ")\n"
                    << "Expected: (" << expected.line << ", " << expected.col
                    << ")";
    }
  }

  EXPECT_EQ(passed, NUM_ITERATIONS);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST_F(ParagraphMotionsTest, ForwardParagraph_AtEndOfBuffer) {
  Lines lines = {"first paragraph", "", "second paragraph"};

  // From last line, } should stay at last line
  Position start(2, 0);
  Position ours = simulateMotions(start, "}", lines);
  Position expected = neovimMotion(lines, 2, 0, "}");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, BackwardParagraph_AtStartOfBuffer) {
  Lines lines = {"first paragraph", "", "second paragraph"};

  // From first line, { should stay at first line (col 0)
  Position start(0, 5);
  Position ours = simulateMotions(start, "{", lines);
  Position expected = neovimMotion(lines, 0, 5, "{");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, ForwardParagraph_OnBlankLine) {
  Lines lines = {"para1", "", "", "para2", "", "para3"};

  // From middle of blank lines, } should go to next blank after para2
  Position start(1, 0);
  Position ours = simulateMotions(start, "}", lines);
  Position expected = neovimMotion(lines, 1, 0, "}");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, BackwardParagraph_OnBlankLine) {
  Lines lines = {"para1", "", "", "para2"};

  // From blank line, { should go to previous blank
  Position start(2, 0);
  Position ours = simulateMotions(start, "{", lines);
  Position expected = neovimMotion(lines, 2, 0, "{");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, CountedMotion_2Forward) {
  Lines lines = {"para1", "", "para2", "", "para3", "", "para4"};

  Position start(0, 0);
  Position ours = simulateMotions(start, "2}", lines);
  Position expected = neovimMotion(lines, 0, 0, "2}");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, CountedMotion_2Backward) {
  Lines lines = {"para1", "", "para2", "", "para3", "", "para4"};

  Position start(6, 0);
  Position ours = simulateMotions(start, "2{", lines);
  Position expected = neovimMotion(lines, 6, 0, "2{");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}
