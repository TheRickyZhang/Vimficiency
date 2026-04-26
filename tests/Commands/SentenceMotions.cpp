// tests/Commands/SentenceMotions.cpp
//
// Tests for sentence motion commands ) and ( using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="SentenceMotionsTest.*"

#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Types/NavContext.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class SentenceMotionsTest : public ::testing::Test {
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
  static CursorPos neovimMotion(const Lines& lines, int startRow, int startCol,
                               const string& keys) {
    auto result = oracle->simulate(lines, startRow, startCol, keys);
    return CursorPos(result.row, result.col);
  }
};

unique_ptr<NeovimOracle> SentenceMotionsTest::oracle;
NavContext SentenceMotionsTest::navContext(0, 0);

// =============================================================================
// Random Buffer Generation for Sentence Testing
// =============================================================================
//
// Sentences need more structured content than paragraphs:
// - Include sentence-ending punctuation [.!?]
// - Include optional closers [)'"']
// - Mix of whitespace and content

struct RandomSentenceTest {
  Lines lines;
  int cursorLine;
  int cursorCol;
};

// Generate a random buffer with sentence structure
RandomSentenceTest generateRandomSentenceBuffer(int numLines) {
  RandomSentenceTest test;

  // Word templates
  const vector<string> words = {
    "hello", "world", "this", "is", "a", "test", "foo", "bar",
    "quick", "brown", "fox", "jumps", "over", "lazy", "dog"
  };

  // Sentence endings
  const vector<string> endings = {".", "!", "?", ".\"", "!'", "?\""};

  for (int i = 0; i < numLines; i++) {
    if (RandomGen::chance(1, 6)) {  // ~17% blank lines
      test.lines.push_back("");
    } else {
      // Content line with sentences
      string line;
      int numSentences = RandomGen::range(1, 3);

      for (int s = 0; s < numSentences; s++) {
        if (s > 0) line += "  ";  // Two spaces between sentences

        int numWords = RandomGen::range(2, 6);
        for (int w = 0; w < numWords; w++) {
          if (w > 0) line += " ";
          string word = RandomGen::pick(words);
          if (w == 0) {
            // Capitalize first letter
            word[0] = static_cast<char>(toupper(word[0]));
          }
          line += word;
        }
        line += RandomGen::pick(endings);
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
    test.lines[0] = "Content here.";
  }

  // Pick a random cursor position
  test.cursorLine = RandomGen::range(0, numLines - 1);

  int lineLen = static_cast<int>(test.lines[test.cursorLine].size());
  if (lineLen > 0) {
    test.cursorCol = RandomGen::range(0, lineLen - 1);
  } else {
    test.cursorCol = 0;
  }

  return test;
}

// =============================================================================
// Random Buffer Tests - ) motion
// =============================================================================

TEST_F(SentenceMotionsTest, ForwardSentence_RandomBuffer) {
  RandomGen::seed(42);
  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int numLines = RandomGen::range(3, 10);
    auto test = generateRandomSentenceBuffer(numLines);

    CursorPos start(test.cursorLine, test.cursorCol);
    CursorPos ours = simulateMovements(start, ")", test.lines);
    CursorPos expected = neovimMotion(test.lines, test.cursorLine, test.cursorCol, ")");

    if (ours.line == expected.line && ours.col == expected.col) {
      passed++;
    } else {
      ADD_FAILURE() << "Iteration " << i << ": ) motion mismatch\n"
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
// Random Buffer Tests - ( motion
// =============================================================================

TEST_F(SentenceMotionsTest, BackwardSentence_RandomBuffer) {
  RandomGen::seed(123);

  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    int numLines = RandomGen::range(3, 10);
    auto test = generateRandomSentenceBuffer(numLines);

    CursorPos start(test.cursorLine, test.cursorCol);
    CursorPos ours = simulateMovements(start, "(", test.lines);
    CursorPos expected = neovimMotion(test.lines, test.cursorLine, test.cursorCol, "(");

    if (ours.line == expected.line && ours.col == expected.col) {
      passed++;
    } else {
      ADD_FAILURE() << "Iteration " << i << ": ( motion mismatch\n"
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

TEST_F(SentenceMotionsTest, ForwardSentence_AtEndOfBuffer) {
  Lines lines = {"First sentence.", "", "Second sentence."};

  // From last line, ) should stay at last position
  CursorPos start(2, 15);  // End of "Second sentence."
  CursorPos ours = simulateMovements(start, ")", lines);
  CursorPos expected = neovimMotion(lines, 2, 15, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, BackwardSentence_AtStartOfBuffer) {
  Lines lines = {"First sentence.", "", "Second sentence."};

  // From first position, ( should stay at first position
  CursorPos start(0, 0);
  CursorPos ours = simulateMovements(start, "(", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, ForwardSentence_OnBlankLine) {
  Lines lines = {"First.", "", "", "Second."};

  // From blank line, ) should go to next sentence start
  CursorPos start(1, 0);
  CursorPos ours = simulateMovements(start, ")", lines);
  CursorPos expected = neovimMotion(lines, 1, 0, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, BackwardSentence_OnBlankLine) {
  Lines lines = {"First.", "", "", "Second."};

  // From blank line, ( should go to previous sentence start
  CursorPos start(2, 0);
  CursorPos ours = simulateMovements(start, "(", lines);
  CursorPos expected = neovimMotion(lines, 2, 0, "(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, ForwardSentence_MultipleSentencesOnLine) {
  Lines lines = {"First. Second. Third."};

  // From start, ) should go to "Second"
  CursorPos start(0, 0);
  CursorPos ours = simulateMovements(start, ")", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, BackwardSentence_MultipleSentencesOnLine) {
  Lines lines = {"First. Second. Third."};

  // From "Third", ( should go to "Second"
  CursorPos start(0, 15);  // At "Third"
  CursorPos ours = simulateMovements(start, "(", lines);
  CursorPos expected = neovimMotion(lines, 0, 15, "(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, ForwardSentence_WithClosers) {
  Lines lines = {"\"Hello!\" she said."};

  // The ! is sentence end, " is closer
  CursorPos start(0, 0);
  CursorPos ours = simulateMovements(start, ")", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, CountedMotion_2Forward) {
  Lines lines = {"First. Second. Third."};

  CursorPos start(0, 0);
  CursorPos ours = simulateMovements(start, "2)", lines);
  CursorPos expected = neovimMotion(lines, 0, 0, "2)");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, CountedMotion_2Backward) {
  Lines lines = {"First. Second. Third."};

  CursorPos start(0, 15);  // At "Third"
  CursorPos ours = simulateMovements(start, "2(", lines);
  CursorPos expected = neovimMotion(lines, 0, 15, "2(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}
