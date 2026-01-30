// tests/Operator/TextObjects.cpp
//
// Tests for operator + text object boundary crossing logic.
// Text objects select a range from cursor position in both directions.
//
// Uses VimCore::textObjectRange for offset-based boundary checking.

#include <gtest/gtest.h>

#include "TestHelpers.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

// =============================================================================
// TextObjectsTest - Tests for text object boundary crossing
// =============================================================================

class TextObjectsTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
  static void TearDownTestSuite() { oracle_.reset(); }
  static unique_ptr<NeovimOracle> oracle_;
};

unique_ptr<NeovimOracle> TextObjectsTest::oracle_;

// =============================================================================
// Text Object Test Infrastructure
// =============================================================================

struct TextObjectSpec {
  string cmd;       // e.g., "diw", "daw"
  bool isInner;     // true for iw/iW, false for aw/aW
  bool isBigWord;   // true for W variants

  // Predict if text object would cross boundaries using VimCore
  // Uses offset-based boundary model: leftColOffset protects cols [0, offset) on line 0,
  // rightColOffset protects cols [lineLen-offset, lineLen) on last line
  bool wouldCross(Position cursor, const Lines& lines,
                  int leftColOffset, int rightColOffset,
                  bool hasLinesAbove, bool hasLinesBelow) const {
    Range range = VimCore::textObjectRange(
        cursor, lines, isInner, isBigWord, leftColOffset, rightColOffset,
        hasLinesAbove, hasLinesBelow);
    // Check if either endpoint crossed boundary
    return range.first == POSITION_OUTSIDE_BOUNDARY || range.last == POSITION_OUTSIDE_BOUNDARY;
  }
};

const vector<TextObjectSpec>& getAllTextObjects() {
  static vector<TextObjectSpec> textObjects = {
      {"diw", true, false},
      {"daw", false, false},
      {"diW", true, true},
      {"daW", false, true},
  };
  return textObjects;
}

// =============================================================================
// Text Object Buffer Generation
// =============================================================================

RandomBufferTest generateTextObjectBuffer(int numLines) {
  RandomBufferTest test;
  test.lines = randomLines(numLines, 10, 20);

  // For text objects, use single-line edit region
  int editLine = RandomGen::range(0, numLines - 1);
  int lineLen2 = static_cast<int>(test.lines[editLine].size());

  // Ensure line is long enough for meaningful edit region
  int minEditLen = 4;
  if (lineLen2 < minEditLen + 2) {
    test.lines[editLine] += string(minEditLen + 2 - lineLen2, 'x');
    lineLen2 = static_cast<int>(test.lines[editLine].size());
  }

  // Pick edit region with room for boundaries
  int editStart = RandomGen::range(1, max(1, lineLen2 - minEditLen - 1));
  int editEnd = RandomGen::range(editStart + minEditLen - 1, min(lineLen2 - 2, editStart + 10));

  test.editStartLine = editLine;
  test.editStartCol = editStart;
  test.editEndLine = editLine;
  test.editEndCol = editEnd;

  // Random cursor position within edit region
  test.cursorCol = RandomGen::range(editStart, editEnd);
  test.cursorLine = editLine;

  // Set boundary positions
  test.hasLeftBoundary = true;
  test.hasRightBoundary = true;
  test.leftBoundaryPos = Position(editLine, editStart - 1);
  test.rightBoundaryPos = Position(editLine, editEnd + 1);

  // Build prefix/suffix
  string& line = test.lines[editLine];
  string prefix;
  for (int i = 0; i < editLine; i++) {
    prefix += test.lines[i] + '\n';
  }
  prefix += line.substr(0, editStart);
  test.prefix = prefix;

  string suffix = line.substr(editEnd + 1);
  for (int i = editLine + 1; i < numLines; i++) {
    suffix += '\n';
    suffix += test.lines[i];
  }
  test.suffix = suffix;

  // Context flags
  test.hasLinesAbove = (editLine > 0);
  test.hasLinesBelow = (editLine < numLines - 1);

  return test;
}

// =============================================================================
// Text Object Test Runner
// =============================================================================

bool runTextObjectTest(NeovimOracle& oracle, const TextObjectSpec& spec,
                       const RandomBufferTest& test, bool verbose = false) {
  // Get actual result from Neovim (uses full buffer)
  auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, spec.cmd);

  bool leftCrossed = test.hasLeftBoundary && leftBoundaryCrossed(test, result.lines);
  bool rightCrossed = test.hasRightBoundary && rightBoundaryCrossed(test, result.lines);

  // === IMPORTANT: effectiveLines Model ===
  // VimCore boundary functions expect effectiveLines, NOT the full buffer.
  // effectiveLines contains only the lines in the edit region, with offsets
  // protecting the prefix/suffix columns. This mirrors EditSearchContext behavior.
  //
  // Example: Full buffer has 3 lines, edit region is on line 0 cols 5-10.
  //   - effectiveLines = [line 0 only]
  //   - leftColOffset = 5 (protects cols 0-4)
  //   - rightColOffset = len(line 0) - 10 - 1 (protects suffix)
  //   - cursor.line = 0 (relative to effectiveLines, not full buffer)
  //
  // Contrast with Words.cpp tests which span full buffer → full buffer IS effectiveLines.
  Lines effectiveLines;
  for (int i = test.editStartLine; i <= test.editEndLine; i++) {
    effectiveLines.push_back(test.lines[i]);
  }
  int effectiveLastLine = static_cast<int>(effectiveLines.size()) - 1;

  // Cursor position relative to effectiveLines (translate from full buffer coords)
  Position cursor(test.cursorLine - test.editStartLine, test.cursorCol);

  // Compute offsets relative to effectiveLines
  // leftColOffset: prefix length on first line of effectiveLines (line 0)
  int leftColOffset = test.hasLeftBoundary ? test.editStartCol : 0;

  // rightColOffset: suffix length on last line of effectiveLines
  int rightColOffset = 0;
  if (test.hasRightBoundary) {
    int lineLen = static_cast<int>(effectiveLines[effectiveLastLine].size());
    rightColOffset = lineLen - test.editEndCol - 1;
  }

  // Predict using VimCore::textObjectRange with effectiveLines (NOT full buffer!)
  bool predictCross = spec.wouldCross(cursor, effectiveLines, leftColOffset, rightColOffset,
                                       test.hasLinesAbove, test.hasLinesBelow);

  bool actualCross = leftCrossed || rightCrossed;

  // Failure if: actual crossed but we predicted safe
  bool success = !(actualCross && !predictCross);

  if (verbose && !success) {
    cerr << "\n=== TEXT OBJECT TEST FAILURE ===" << endl;
    cerr << "Command: " << spec.cmd << endl;
    cerr << "Input:" << endl;
    for (size_t i = 0; i < test.lines.size(); i++) {
      cerr << "  [" << i << "]: \"" << test.lines[i] << "\"" << endl;
    }
    cerr << "Cursor: (" << test.cursorLine << ", " << test.cursorCol << ")" << endl;
    cerr << "Edit region: (" << test.editStartLine << "," << test.editStartCol << ") to ("
         << test.editEndLine << "," << test.editEndCol << ")" << endl;
    cerr << "Left offset: " << leftColOffset << ", Right offset: " << rightColOffset << endl;
    cerr << "Result:" << endl;
    for (size_t i = 0; i < result.lines.size(); i++) {
      cerr << "  [" << i << "]: \"" << result.lines[i] << "\"" << endl;
    }
    cerr << "Predicted: " << (predictCross ? "CROSS" : "SAFE")
         << ", Actual: " << (actualCross ? "CROSSED" : "SAFE") << endl;
  }

  return success;
}

// =============================================================================
// Section 1: Random Buffer Stress Tests
// =============================================================================

TEST_F(TextObjectsTest, RandomBufferStress_SingleLine) {
  RandomGen::seed(42);
  const int NUM_BUFFERS = 10;
  const auto& textObjects = getAllTextObjects();

  int total = 0, passed = 0;

  for (int i = 0; i < NUM_BUFFERS; i++) {
    auto test = generateTextObjectBuffer(1);

    for (const auto& spec : textObjects) {
      total++;
      if (runTextObjectTest(*oracle_, spec, test, true)) {
        passed++;
      }
    }
  }

  EXPECT_EQ(passed, total);
}

TEST_F(TextObjectsTest, RandomBufferStress_MultiLine) {
  RandomGen::seed(123);
  const int NUM_BUFFERS = 10;
  const auto& textObjects = getAllTextObjects();

  int total = 0, passed = 0;

  for (int i = 0; i < NUM_BUFFERS; i++) {
    int numLines = RandomGen::range(2, 4);
    auto test = generateTextObjectBuffer(numLines);

    for (const auto& spec : textObjects) {
      total++;
      if (runTextObjectTest(*oracle_, spec, test, true)) {
        passed++;
      }
    }
  }

  EXPECT_EQ(passed, total);
}

// =============================================================================
// Section 2: Manual Edge Case Tests
// =============================================================================

TEST_F(TextObjectsTest, Diw_InMiddleOfWord) {
  Lines lines = {"hello world"};
  auto result = oracle_->simulate(lines, 0, 2, "diw");  // Cursor on 'l' in hello
  // Should delete "hello", leaving " world"
  EXPECT_EQ(result.lines[0], " world");
}

TEST_F(TextObjectsTest, Diw_OnWhitespace) {
  Lines lines = {"hello   world"};
  auto result = oracle_->simulate(lines, 0, 6, "diw");  // Cursor in whitespace
  // Should delete the whitespace
  EXPECT_EQ(result.lines[0], "helloworld");
}

TEST_F(TextObjectsTest, Daw_WithTrailingWhitespace) {
  Lines lines = {"hello   world"};
  auto result = oracle_->simulate(lines, 0, 0, "daw");  // Cursor on 'h'
  // Should delete "hello   " (word + trailing whitespace)
  EXPECT_EQ(result.lines[0], "world");
}

TEST_F(TextObjectsTest, Daw_WithNoTrailingWhitespace) {
  Lines lines = {"hello"};
  auto result = oracle_->simulate(lines, 0, 0, "daw");  // Cursor on 'h', no trailing
  // Should delete entire word
  EXPECT_EQ(result.lines[0], "");
}

TEST_F(TextObjectsTest, Daw_LastWordWithLeadingWhitespace) {
  Lines lines = {"hello world"};
  auto result = oracle_->simulate(lines, 0, 6, "daw");  // Cursor on 'w' in world
  // Should delete " world" (leading whitespace + word, since no trailing)
  EXPECT_EQ(result.lines[0], "hello");
}

TEST_F(TextObjectsTest, DiW_OnSymbol) {
  Lines lines = {"hello... world"};  // Note: space before world
  auto result = oracle_->simulate(lines, 0, 0, "diW");  // Cursor on 'h'
  // WORD treats "hello..." as one word (no whitespace in it)
  EXPECT_EQ(result.lines[0], " world");
}

TEST_F(TextObjectsTest, DaW_WithTrailingWhitespace) {
  Lines lines = {"hello...  world"};
  auto result = oracle_->simulate(lines, 0, 0, "daW");  // Cursor on 'h'
  // Should delete "hello...  "
  EXPECT_EQ(result.lines[0], "world");
}

// =============================================================================
// Section 3: Boundary Checking Against VimCore
// =============================================================================

TEST_F(TextObjectsTest, TextObjectRange_InnerWord) {
  // Test that textObjectRange correctly computes range for diw
  Lines lines = {"abc def ghi"};
  Position cursor(0, 4);           // On 'd' in "def"
  // Old Position boundary at col 3 → protect cols [0, 4), so leftColOffset = 4
  // Old Position boundary at col 7 → protect cols [7, 11), so rightColOffset = 11 - 7 = 4
  int leftColOffset = 4;   // protect cols 0-3 (space before "def")
  int rightColOffset = 4;  // protect cols 7-10 (space after "def" and beyond)

  Range result = VimCore::textObjectRange(cursor, lines, true, false,
                                          leftColOffset, rightColOffset,
                                          false, false);  // single line, no lines above/below

  // diw should NOT reach the boundaries (only selects "def" at cols 4-6)
  EXPECT_NE(result.first, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TextObjectsTest, TextObjectRange_AroundWord) {
  // Test that textObjectRange correctly computes range for daw
  Lines lines = {"abc def ghi"};
  Position cursor(0, 4);           // On 'd' in "def"
  // Old Position boundary at col 2 → protect cols [0, 3), so leftColOffset = 3
  // Old Position boundary at col 8 → protect cols [8, 11), so rightColOffset = 11 - 8 = 3
  int leftColOffset = 3;   // protect cols 0-2 ('c' in "abc" and before)
  int rightColOffset = 3;  // protect cols 8-10 ('g' in "ghi" and beyond)

  Range result = VimCore::textObjectRange(cursor, lines, false, false,
                                          leftColOffset, rightColOffset,
                                          false, false);  // single line, no lines above/below

  // daw should NOT reach the boundaries (selects "def " at cols 4-7)
  EXPECT_NE(result.first, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TextObjectsTest, TextObjectRange_CrossesBoundary) {
  // Test where text object DOES cross a boundary
  Lines lines = {"abc"};
  Position cursor(0, 1);           // On 'b'
  // Old Position boundary at col 0 → protect cols [0, 1), so leftColOffset = 1
  // Old Position boundary at col 2 → protect cols [2, 3), so rightColOffset = 3 - 2 = 1
  int leftColOffset = 1;   // protect col 0 ('a' - adjacent to word)
  int rightColOffset = 1;  // protect col 2 ('c' - adjacent to word)

  Range result = VimCore::textObjectRange(cursor, lines, true, false,
                                          leftColOffset, rightColOffset,
                                          false, false);  // single line, no lines above/below

  // diw on "abc" should reach both boundaries (selects entire word at cols 0-2)
  EXPECT_EQ(result.first, POSITION_OUTSIDE_BOUNDARY);
}

TEST_F(TextObjectsTest, TextObjectRange_NoBoundary) {
  // When no boundaries are set (offset = 0), text object always succeeds
  Lines lines = {"hello world"};
  Position cursor(0, 0);

  // Use 0 to indicate no boundary check, no lines above/below
  Range result = VimCore::textObjectRange(cursor, lines, true, false, 0, 0, false, false);

  EXPECT_NE(result.first, POSITION_OUTSIDE_BOUNDARY);
  EXPECT_EQ(result.first.col, 0);
  EXPECT_EQ(result.last.col, 4);  // "hello"
}
