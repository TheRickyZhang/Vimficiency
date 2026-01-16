// tests/Commands/ParagraphMotions.cpp
//
// Tests for paragraph motion commands (}, {) using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Motion semantics from boundary-logic.md:
//   }  = (Forward, NextEdge)   - move to first blank line after paragraph
//   {  = (Backward, NextEdge)  - move to first blank line before paragraph

#include <gtest/gtest.h>

#include <random>

#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimMovementUtils.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class ParagraphMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;

  static void SetUpTestSuite() {
    oracle = make_unique<NeovimOracle>();
    navContext = NavContext(39, 19);
  }

  static void TearDownTestSuite() { oracle.reset(); }

  // Apply motion using our implementation
  static Position applyMotion(Position start, const string& motion,
                              const Lines& lines) {
    return simulateMotions(start, Mode::Normal, navContext, motion, lines).pos;
  }

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
RandomParagraphTest generateRandomParagraphBuffer(mt19937& rng, int numLines) {
  RandomParagraphTest test;

  // Simple content for non-blank lines
  const string contentChars = "abcdefghijklmnopqrstuvwxyz";

  uniform_int_distribution<int> blankDist(0, 3);  // ~25% blank lines
  uniform_int_distribution<int> lineLen(5, 30);

  for (int i = 0; i < numLines; i++) {
    if (blankDist(rng) == 0) {
      // Blank line (empty or whitespace-only)
      test.lines.push_back("");
    } else {
      // Content line
      int len = lineLen(rng);
      string line;
      line.reserve(len);
      for (int j = 0; j < len; j++) {
        line += contentChars[rng() % contentChars.size()];
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
  uniform_int_distribution<int> lineDist(0, numLines - 1);
  test.cursorLine = lineDist(rng);

  int lineLen2 = test.lines[test.cursorLine].size();
  if (lineLen2 > 0) {
    uniform_int_distribution<int> colDist(0, lineLen2 - 1);
    test.cursorCol = colDist(rng);
  } else {
    test.cursorCol = 0;
  }

  return test;
}

// =============================================================================
// Random Buffer Tests - } motion
// =============================================================================

TEST_F(ParagraphMotionsTest, ForwardParagraph_RandomBuffer) {
  mt19937 rng(42);
  const int NUM_ITERATIONS = 100;

  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    uniform_int_distribution<int> linesDist(3, 15);
    auto test = generateRandomParagraphBuffer(rng, linesDist(rng));

    Position start(test.cursorLine, test.cursorCol);
    Position ours = applyMotion(start, "}", test.lines);
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
  mt19937 rng(123);
  const int NUM_ITERATIONS = 100;

  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    uniform_int_distribution<int> linesDist(3, 15);
    auto test = generateRandomParagraphBuffer(rng, linesDist(rng));

    Position start(test.cursorLine, test.cursorCol);
    Position ours = applyMotion(start, "{", test.lines);
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
  Position ours = applyMotion(start, "}", lines);
  Position expected = neovimMotion(lines, 2, 0, "}");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, BackwardParagraph_AtStartOfBuffer) {
  Lines lines = {"first paragraph", "", "second paragraph"};

  // From first line, { should stay at first line (col 0)
  Position start(0, 5);
  Position ours = applyMotion(start, "{", lines);
  Position expected = neovimMotion(lines, 0, 5, "{");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, ForwardParagraph_OnBlankLine) {
  Lines lines = {"para1", "", "", "para2", "", "para3"};

  // From middle of blank lines, } should go to next blank after para2
  Position start(1, 0);
  Position ours = applyMotion(start, "}", lines);
  Position expected = neovimMotion(lines, 1, 0, "}");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, BackwardParagraph_OnBlankLine) {
  Lines lines = {"para1", "", "", "para2"};

  // From blank line, { should go to previous blank
  Position start(2, 0);
  Position ours = applyMotion(start, "{", lines);
  Position expected = neovimMotion(lines, 2, 0, "{");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, CountedMotion_2Forward) {
  Lines lines = {"para1", "", "para2", "", "para3", "", "para4"};

  Position start(0, 0);
  Position ours = applyMotion(start, "2}", lines);
  Position expected = neovimMotion(lines, 0, 0, "2}");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(ParagraphMotionsTest, CountedMotion_2Backward) {
  Lines lines = {"para1", "", "para2", "", "para3", "", "para4"};

  Position start(6, 0);
  Position ours = applyMotion(start, "2{", lines);
  Position expected = neovimMotion(lines, 6, 0, "2{");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}
