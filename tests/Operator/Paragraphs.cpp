// tests/Operator/Paragraphs.cpp
//
// Tests for paragraph-based operator commands (dip, dap, d}, d{).
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="ParagraphsTest.*"
//   dap: depends on cursor position and trailing blank lines
//   d}:  delete from cursor to next blank line (linewise behavior)
//   d{:  delete from cursor to previous blank line (linewise behavior)

#include <gtest/gtest.h>

#include "VimTypes/LineRange.h"
#include "VimTypes/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class ParagraphsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle_;
  static const int NUM_ITERATIONS = 50;
  static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle_.reset(); }
};

unique_ptr<NeovimOracle> ParagraphsTest::oracle_;

// =============================================================================
// Random Buffer Generation for Paragraph Boundary Testing
// =============================================================================
//
// For boundary testing, we need:
// - An edit region (range of lines we're allowed to modify)
// - Reserved marker lines at boundaries (so we can detect crossing)
// - Random paragraph structure within the edit region

struct ParagraphBoundaryTest {
  Lines lines;

  // Edit region (line indices, inclusive)
  int editStartLine;
  int editEndLine;

  // Cursor position (within edit region)
  int cursorLine;
  int cursorCol;

  // Boundary lines (just OUTSIDE the edit region)
  int topBoundaryLine;     // -1 if no top boundary (at buffer start)
  int bottomBoundaryLine;  // -1 if no bottom boundary (at buffer end)

  // Marker content for verification
  string topMarker;
  string bottomMarker;

  bool hasTopBoundary;
  bool hasBottomBoundary;
};

// Generate a random buffer with paragraph structure and boundary markers
ParagraphBoundaryTest generateParagraphBoundaryBuffer(int numLines) {
  ParagraphBoundaryTest test;

  // Ensure minimum size for meaningful test
  numLines = max(numLines, 5);

  // Reserve markers for boundaries (unique strings unlikely in random content)
  test.topMarker = "TOP_BOUNDARY_MARKER_12345";
  test.bottomMarker = "BOTTOM_BOUNDARY_MARKER_67890";

  // Generate buffer content
  for (int i = 0; i < numLines; i++) {
    if (RandomGen::chance(1, 3)) {  // ~33% blank lines
      test.lines.push_back("");
    } else {
      int len = RandomGen::range(5, 20);
      string line;
      for (int j = 0; j < len; j++) {
        line += RandomGen::pick(CharPools::LETTERS);
      }
      test.lines.push_back(line);
    }
  }

  // Pick edit region (at least 3 lines)
  int editSize = max(3, numLines - 2);  // Leave room for boundaries
  test.editStartLine = RandomGen::range(1, max(1, numLines - editSize - 1));
  test.editEndLine = min(test.editStartLine + editSize - 1, numLines - 2);

  // Insert boundary markers
  test.hasTopBoundary = (test.editStartLine > 0);
  test.hasBottomBoundary = (test.editEndLine < numLines - 1);

  if (test.hasTopBoundary) {
    test.topBoundaryLine = test.editStartLine - 1;
    test.lines[test.topBoundaryLine] = test.topMarker;
  } else {
    test.topBoundaryLine = -1;
  }

  if (test.hasBottomBoundary) {
    test.bottomBoundaryLine = test.editEndLine + 1;
    test.lines[test.bottomBoundaryLine] = test.bottomMarker;
  } else {
    test.bottomBoundaryLine = -1;
  }

  // Ensure edit region has at least one non-blank line
  bool hasContent = false;
  for (int i = test.editStartLine; i <= test.editEndLine; i++) {
    if (!test.lines[i].empty()) {
      hasContent = true;
      break;
    }
  }
  if (!hasContent) {
    test.lines[test.editStartLine] = "content";
  }

  // Pick cursor position within edit region
  test.cursorLine = RandomGen::range(test.editStartLine, test.editEndLine);

  int lineLen2 = test.lines[test.cursorLine].size();
  if (lineLen2 > 0) {
    test.cursorCol = RandomGen::range(0, lineLen2 - 1);
  } else {
    test.cursorCol = 0;
  }

  return test;
}

// Check if top boundary was crossed (marker line deleted or modified)
bool topBoundaryCrossed(const ParagraphBoundaryTest& test, const Lines& result) {
  if (!test.hasTopBoundary) return false;

  // Check if marker still exists at expected position
  if (test.topBoundaryLine >= (int)result.size()) return true;

  // Check if line content matches marker
  return result[test.topBoundaryLine] != test.topMarker;
}

// Check if bottom boundary was crossed (marker line deleted or modified)
bool bottomBoundaryCrossed(const ParagraphBoundaryTest& test, const Lines& result) {
  if (!test.hasBottomBoundary) return false;

  // Find where bottom marker should be (accounting for deleted lines above)
  // This is tricky - we need to find if the marker exists anywhere
  for (const auto& line : result) {
    if (line == test.bottomMarker) return false;  // Marker found, not crossed
  }
  return true;  // Marker not found, was crossed
}

// =============================================================================
// Section 1: paragraphTextObjectRange Tests
// =============================================================================
//
// Verify that our range computation matches what Neovim actually deletes

TEST_F(ParagraphsTest, InnerParagraph_RangeComputation) {
  // Test dip range computation
  Lines lines = {"first", "second", "", "third", "fourth"};

  // On non-blank line "second"
  LineRange range = VimCore::paragraphTextObjectRange(1, lines, true);
  EXPECT_EQ(range.firstLine, 0);  // Start of paragraph block
  EXPECT_EQ(range.lastLine, 1);    // End of paragraph block

  // On blank line
  LineRange rangeBlank = VimCore::paragraphTextObjectRange(2, lines, true);
  EXPECT_EQ(rangeBlank.firstLine, 2);  // Just the blank line
  EXPECT_EQ(rangeBlank.lastLine, 2);
}

TEST_F(ParagraphsTest, AroundParagraph_WithTrailingBlanks) {
  Lines lines = {"first", "second", "", "", "third"};

  // On non-blank line with trailing blanks
  LineRange range = VimCore::paragraphTextObjectRange(0, lines, false);
  // Should include paragraph + trailing blanks
  EXPECT_EQ(range.firstLine, 0);
  EXPECT_EQ(range.lastLine, 3);  // Through blank lines
}

TEST_F(ParagraphsTest, AroundParagraph_NoTrailingBlanks) {
  Lines lines = {"", "first", "second"};

  // On non-blank line with no trailing blanks, should include leading blanks
  LineRange range = VimCore::paragraphTextObjectRange(1, lines, false);
  EXPECT_EQ(range.firstLine, 0);  // Include leading blank
  EXPECT_EQ(range.lastLine, 2);
}

TEST_F(ParagraphsTest, AroundParagraph_OnBlankLine) {
  Lines lines = {"first", "", "", "second", "third"};

  // On blank line, should include blank run + following paragraph
  LineRange range = VimCore::paragraphTextObjectRange(1, lines, false);
  EXPECT_EQ(range.firstLine, 1);  // Start of blank run
  EXPECT_EQ(range.lastLine, 4);    // End of following paragraph
}

// =============================================================================
// Section 2: Neovim-Verified Random Buffer Tests
// =============================================================================

TEST_F(ParagraphsTest, InnerParagraph_RandomBuffer) {
  RandomGen::seed(42);
  int total = 0, passed = 0;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int numLines = RandomGen::range(5, 12);
    auto test = generateParagraphBoundaryBuffer(numLines);

    total++;

    // Execute dip in Neovim
    auto result = oracle_->simulate(test.lines, test.cursorLine, test.cursorCol, "dip");

    // Check if boundaries were crossed
    bool topCrossed = topBoundaryCrossed(test, result.lines);
    bool bottomCrossed = bottomBoundaryCrossed(test, result.lines);

    // Compute our predicted range
    LineRange range = VimCore::paragraphTextObjectRange(test.cursorLine, test.lines, true);

    // Check our prediction
    bool predictTopCross = test.hasTopBoundary && range.firstLine <= test.topBoundaryLine;
    bool predictBottomCross = test.hasBottomBoundary && range.lastLine >= test.bottomBoundaryLine;

    // Failure if: actual crossed but we predicted safe
    bool failure = (topCrossed && !predictTopCross) || (bottomCrossed && !predictBottomCross);

    if (!failure) {
      passed++;
    } else {
      ADD_FAILURE() << "dip boundary prediction failed at iteration " << i << "\n"
                    << "Cursor: (" << test.cursorLine << ", " << test.cursorCol << ")\n"
                    << "Edit region: [" << test.editStartLine << ", " << test.editEndLine << "]\n"
                    << "Top boundary: " << test.topBoundaryLine << ", Bottom: " << test.bottomBoundaryLine << "\n"
                    << "Our range: [" << range.firstLine << ", " << range.lastLine << "]\n"
                    << "Top - Predicted: " << (predictTopCross ? "CROSS" : "SAFE")
                    << ", Actual: " << (topCrossed ? "CROSSED" : "SAFE") << "\n"
                    << "Bottom - Predicted: " << (predictBottomCross ? "CROSS" : "SAFE")
                    << ", Actual: " << (bottomCrossed ? "CROSSED" : "SAFE");
    }
  }

  EXPECT_EQ(passed, total) << "=== Inner Paragraph: " << passed << "/" << total << " ===";
}

TEST_F(ParagraphsTest, AroundParagraph_RandomBuffer) {
  RandomGen::seed(123);

  int total = 0, passed = 0;

  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int numLines = RandomGen::range(5, 12);
    auto test = generateParagraphBoundaryBuffer(numLines);

    total++;

    // Execute dap in Neovim
    auto result = oracle_->simulate(test.lines, test.cursorLine, test.cursorCol, "dap");

    // Check if boundaries were crossed
    bool topCrossed = topBoundaryCrossed(test, result.lines);
    bool bottomCrossed = bottomBoundaryCrossed(test, result.lines);

    // Compute our predicted range
    LineRange range = VimCore::paragraphTextObjectRange(test.cursorLine, test.lines, false);

    // Check our prediction
    bool predictTopCross = test.hasTopBoundary && range.firstLine <= test.topBoundaryLine;
    bool predictBottomCross = test.hasBottomBoundary && range.lastLine >= test.bottomBoundaryLine;

    // Failure if: actual crossed but we predicted safe
    bool failure = (topCrossed && !predictTopCross) || (bottomCrossed && !predictBottomCross);

    if (!failure) {
      passed++;
    } else {
      ADD_FAILURE() << "dap boundary prediction failed at iteration " << i << "\n"
                    << "Cursor: (" << test.cursorLine << ", " << test.cursorCol << ")\n"
                    << "Edit region: [" << test.editStartLine << ", " << test.editEndLine << "]\n"
                    << "Top boundary: " << test.topBoundaryLine << ", Bottom: " << test.bottomBoundaryLine << "\n"
                    << "Our range: [" << range.firstLine << ", " << range.lastLine << "]\n"
                    << "Top - Predicted: " << (predictTopCross ? "CROSS" : "SAFE")
                    << ", Actual: " << (topCrossed ? "CROSSED" : "SAFE") << "\n"
                    << "Bottom - Predicted: " << (predictBottomCross ? "CROSS" : "SAFE")
                    << ", Actual: " << (bottomCrossed ? "CROSSED" : "SAFE");
    }
  }

  EXPECT_EQ(passed, total) << "=== Around Paragraph: " << passed << "/" << total << " ===";
}

// =============================================================================
// Section 3: Edge Case Tests
// =============================================================================

TEST_F(ParagraphsTest, Dip_SingleLineBuffer) {
  Lines lines = {"only line"};

  LineRange range = VimCore::paragraphTextObjectRange(0, lines, true);
  EXPECT_EQ(range.firstLine, 0);
  EXPECT_EQ(range.lastLine, 0);

  // Verify against Neovim
  auto result = oracle_->simulate(lines, 0, 0, "dip");
  // Should delete the entire buffer content (empty result)
  EXPECT_TRUE(result.lines.empty() || (result.lines.size() == 1 && result.lines[0].empty()));
}

TEST_F(ParagraphsTest, Dap_SingleLineBuffer) {
  Lines lines = {"only line"};

  LineRange range = VimCore::paragraphTextObjectRange(0, lines, false);
  EXPECT_EQ(range.firstLine, 0);
  EXPECT_EQ(range.lastLine, 0);
}

TEST_F(ParagraphsTest, Dip_AllBlankLines) {
  Lines lines = {"", "", ""};

  // On blank line, dip selects contiguous blank run
  LineRange range = VimCore::paragraphTextObjectRange(1, lines, true);
  EXPECT_EQ(range.firstLine, 0);
  EXPECT_EQ(range.lastLine, 2);
}

TEST_F(ParagraphsTest, Dap_AtBufferEnd) {
  Lines lines = {"content", "", "last paragraph"};

  // dap at end with no trailing blanks should include leading blanks
  LineRange range = VimCore::paragraphTextObjectRange(2, lines, false);
  EXPECT_EQ(range.firstLine, 1);  // Include leading blank
  EXPECT_EQ(range.lastLine, 2);
}

TEST_F(ParagraphsTest, Dap_AtBufferStart) {
  Lines lines = {"first paragraph", "", "content"};

  // dap at start with trailing blanks
  LineRange range = VimCore::paragraphTextObjectRange(0, lines, false);
  EXPECT_EQ(range.firstLine, 0);
  EXPECT_EQ(range.lastLine, 1);  // Include trailing blank
}
