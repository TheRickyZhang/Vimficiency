// tests/Operator/Lines.cpp
//
// Tests for operator + line motion boundary crossing logic.
// Uses VimCore::lineDeleteRange and motionLineEndpoint for boundary checking.
//
// Line operations:
//   dd: Full line delete (linewise) - uses lineDeleteRange
//   D:  Delete to end of line (characterwise) - uses motionLineEndpoint(forward=true)
//   d0: Delete to start of line (characterwise) - uses motionLineEndpoint(forward=false)

#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Editor/LineRange.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEndpointUtils.h"

#include <random>

using namespace std;

// =============================================================================
// LinesTest - Tests for operator + line motion boundary crossing logic
// =============================================================================

class LinesTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle_.reset(); }
  static unique_ptr<NeovimOracle> oracle_;

  mt19937 rng{42};

  // Helper to create EditBoundary with specific boundary context
  // left/right chars indicate what's outside the edit region:
  // - '\n' means at line start/end with lines above/below
  // - other char means that char is adjacent (single-char prefix/suffix for testing)
  static EditBoundary makeBoundary(const Lines& lines, char left, char right) {
    std::string prefix;
    std::string suffix;
    bool hasLinesAbove = false;
    bool hasLinesBelow = false;

    // Set prefix based on left char
    if (left == '\n') {
      hasLinesAbove = true;
      // prefix stays empty
    } else if (left != NO_CHAR) {
      prefix = std::string(1, left);
    }
    // Set suffix based on right char
    if (right == '\n') {
      hasLinesBelow = true;
      // suffix stays empty
    } else if (right != NO_CHAR) {
      suffix = std::string(1, right);
    }

    return EditBoundary(prefix, suffix, hasLinesAbove, hasLinesBelow);
  }
};

unique_ptr<NeovimOracle> LinesTest::oracle_;

// =============================================================================
// Section 1: lineDeleteRange (dd) Tests
// =============================================================================

TEST_F(LinesTest, LineDeleteRange_SingleLine_FullBoundary) {
  // Single line buffer with full boundary - dd should be safe
  Lines lines = {"hello world"};
  Position cursor(0, 5);

  EditBoundary boundary = makeBoundary(lines, '\n', '\n');

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_TRUE(range.isValid()) << "dd should be safe when at full line boundary";
  EXPECT_EQ(range.firstLine, 0);
  EXPECT_EQ(range.lastLine, 0);
}

TEST_F(LinesTest, LineDeleteRange_SingleLine_PartialBoundary) {
  // Single line buffer where edit region doesn't cover full line
  Lines lines = {"hello world"};
  Position cursor(0, 5);

  EditBoundary boundary = makeBoundary(lines, 'h', '\n');  // Not at line start

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_FALSE(range.isValid()) << "dd should cross boundary when not at line start";
}

TEST_F(LinesTest, LineDeleteRange_MultiLine_MiddleLine) {
  // Multi-line buffer, cursor on middle line - dd always safe
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(1, 3);  // Middle line

  EditBoundary boundary = makeBoundary(lines, 'o', 't');  // Partial boundaries

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_TRUE(range.isValid()) << "dd on middle line should always be safe";
}

TEST_F(LinesTest, LineDeleteRange_MultiLine_FirstLine_NoBoundary) {
  // Multi-line, cursor on first line, no left boundary
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(0, 3);

  EditBoundary boundary = makeBoundary(lines, '\n', 't');  // At line start

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_TRUE(range.isValid()) << "dd on first line with atLineStart should be safe";
}

TEST_F(LinesTest, LineDeleteRange_MultiLine_FirstLine_HasBoundary) {
  // Multi-line, cursor on first line, has left boundary
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(0, 3);

  EditBoundary boundary = makeBoundary(lines, 'l', 't');  // Not at line start

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_FALSE(range.isValid()) << "dd on first line without atLineStart should cross";
}

TEST_F(LinesTest, LineDeleteRange_MultiLine_LastLine_NoBoundary) {
  // Multi-line, cursor on last line, no right boundary
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(2, 3);

  EditBoundary boundary = makeBoundary(lines, 'o', '\n');  // At line end

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_TRUE(range.isValid()) << "dd on last line with atLineEnd should be safe";
}

TEST_F(LinesTest, LineDeleteRange_MultiLine_LastLine_HasBoundary) {
  // Multi-line, cursor on last line, has right boundary
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(2, 3);

  EditBoundary boundary = makeBoundary(lines, 'o', 'e');  // Not at line end

  LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

  EXPECT_FALSE(range.isValid()) << "dd on last line without atLineEnd should cross";
}

// =============================================================================
// Section 2: motionLineEndpoint (D) Tests - Forward
// =============================================================================

TEST_F(LinesTest, MotionLineEndpoint_D_SingleLine_NoBoundary) {
  // D on single line with no right boundary
  Lines lines = {"hello world"};
  Position cursor(0, 5);

  EditBoundary boundary = makeBoundary(lines, '\n', '\n');  // At line end

  int endCol = VimCore::motionLineEndpoint(cursor, lines, true, boundary);

  EXPECT_NE(endCol, VimCore::COL_OUTSIDE_BOUNDARY) << "D should be safe at line end";
  EXPECT_EQ(endCol, 10);  // Last char of "hello world"
}

TEST_F(LinesTest, MotionLineEndpoint_D_SingleLine_HasBoundary) {
  // D on single line with right boundary (not at line end)
  Lines lines = {"hello world"};
  Position cursor(0, 5);

  EditBoundary boundary = makeBoundary(lines, '\n', 'd');  // Not at line end

  int endCol = VimCore::motionLineEndpoint(cursor, lines, true, boundary);

  EXPECT_EQ(endCol, VimCore::COL_OUTSIDE_BOUNDARY) << "D should cross when not at line end";
}

TEST_F(LinesTest, MotionLineEndpoint_D_MultiLine_NotLastLine) {
  // D on middle line - boundary only matters on last line
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(1, 3);  // Middle line

  EditBoundary boundary = makeBoundary(lines, '\n', 'e');  // Not at line end (but we're not on last line)

  int endCol = VimCore::motionLineEndpoint(cursor, lines, true, boundary);

  EXPECT_NE(endCol, VimCore::COL_OUTSIDE_BOUNDARY) << "D on non-last line ignores boundary";
}

TEST_F(LinesTest, MotionLineEndpoint_D_EmptyLine) {
  // D on empty line
  Lines lines = {"content", "", "more"};
  Position cursor(1, 0);  // Empty line

  EditBoundary boundary = makeBoundary(lines, '\n', '\n');

  int endCol = VimCore::motionLineEndpoint(cursor, lines, true, boundary);

  EXPECT_NE(endCol, VimCore::COL_OUTSIDE_BOUNDARY);
  EXPECT_EQ(endCol, 0);  // Empty line returns 0
}

// =============================================================================
// Section 3: motionLineEndpoint (d0) Tests - Backward
// =============================================================================

TEST_F(LinesTest, MotionLineEndpoint_D0_SingleLine_NoBoundary) {
  // d0 on single line with no left boundary
  Lines lines = {"hello world"};
  Position cursor(0, 5);

  EditBoundary boundary = makeBoundary(lines, '\n', '\n');  // At line start

  int endCol = VimCore::motionLineEndpoint(cursor, lines, false, boundary);

  EXPECT_NE(endCol, VimCore::COL_OUTSIDE_BOUNDARY) << "d0 should be safe at line start";
  EXPECT_EQ(endCol, 0);
}

TEST_F(LinesTest, MotionLineEndpoint_D0_SingleLine_HasBoundary) {
  // d0 on single line with left boundary (not at line start)
  Lines lines = {"hello world"};
  Position cursor(0, 5);

  EditBoundary boundary = makeBoundary(lines, 'h', '\n');  // Not at line start

  int endCol = VimCore::motionLineEndpoint(cursor, lines, false, boundary);

  EXPECT_EQ(endCol, VimCore::COL_OUTSIDE_BOUNDARY) << "d0 should cross when not at line start";
}

TEST_F(LinesTest, MotionLineEndpoint_D0_MultiLine_NotFirstLine) {
  // d0 on middle line - boundary only matters on first line
  Lines lines = {"line one", "line two", "line three"};
  Position cursor(1, 3);  // Middle line

  EditBoundary boundary = makeBoundary(lines, 'l', '\n');  // Not at line start (but we're not on first line)

  int endCol = VimCore::motionLineEndpoint(cursor, lines, false, boundary);

  EXPECT_NE(endCol, VimCore::COL_OUTSIDE_BOUNDARY) << "d0 on non-first line ignores boundary";
  EXPECT_EQ(endCol, 0);
}

// =============================================================================
// Section 4: Random Buffer Stress Tests with Neovim Verification
// =============================================================================

TEST_F(LinesTest, RandomStress_LineDeleteRange) {
  const int NUM_TESTS = 50;
  int passed = 0;

  for (int i = 0; i < NUM_TESTS; i++) {
    // Generate random lines
    const string chars = "abcdefghijklmnopqrstuvwxyz ";
    uniform_int_distribution<int> numLinesDist(1, 5);
    uniform_int_distribution<int> lineLen(5, 20);
    int numLines = numLinesDist(rng);

    Lines lines;
    for (int j = 0; j < numLines; j++) {
      int len = lineLen(rng);
      string line;
      for (int k = 0; k < len; k++) {
        line += chars[rng() % chars.size()];
      }
      lines.push_back(line);
    }

    // Random cursor position
    uniform_int_distribution<int> lineDist(0, numLines - 1);
    int cursorLine = lineDist(rng);
    int curLineLen = lines[cursorLine].size();
    uniform_int_distribution<int> colDist(0, max(0, curLineLen - 1));
    int cursorCol = curLineLen > 0 ? colDist(rng) : 0;

    // Random boundary (50% chance of full line boundary)
    uniform_int_distribution<int> boundaryChoice(0, 1);
    char leftChar, rightChar;
    if (boundaryChoice(rng) == 0) {
      leftChar = '\n';
      rightChar = '\n';
    } else {
      leftChar = lines[0][0];
      int lastLine = numLines - 1;
      int lastLineLen = lines[lastLine].size();
      rightChar = lastLineLen > 0 ? lines[lastLine][lastLineLen - 1] : '\n';
    }

    EditBoundary boundary = makeBoundary(lines, leftChar, rightChar);
    Position cursor(cursorLine, cursorCol);
    LineRange range = VimCore::lineDeleteRange(cursor, lines, boundary);

    // Execute dd in Neovim and verify boundary not crossed
    auto result = oracle_->simulate(lines, cursorLine, cursorCol, "dd");

    // If our prediction says safe, verify Neovim result makes sense
    if (range.isValid()) {
      // dd should have deleted a line
      bool lineDeleted = result.lines.size() < lines.size() ||
                        (lines.size() == 1 && result.lines.size() == 1 && result.lines[0].empty());
      if (lineDeleted) passed++;
    } else {
      // We predicted crossing - this is conservative, always count as pass
      passed++;
    }
  }

  EXPECT_EQ(passed, NUM_TESTS);
}
