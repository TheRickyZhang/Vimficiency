// tests/Commands/SentenceMotions.cpp
//
// Tests for sentence motion commands () and () using random buffers
// and NeovimOracle verification. These test pure cursor movement, not deletion.
//
// Motion semantics from boundary-logic.md:
//   )  = (Forward, NextEdge)   - move to start of next sentence
//   (  = (Backward, NextEdge)  - move to start of previous sentence

#include <gtest/gtest.h>

#include <random>

#include "Editor/Motion.h"
#include "Editor/NavContext.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

using namespace std;

// =============================================================================
// Test Infrastructure
// =============================================================================

class SentenceMotionsTest : public ::testing::Test {
protected:
  static unique_ptr<NeovimOracle> oracle;
  static NavContext navContext;

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
RandomSentenceTest generateRandomSentenceBuffer(mt19937& rng, int numLines) {
  RandomSentenceTest test;

  // Word templates
  const vector<string> words = {
    "hello", "world", "this", "is", "a", "test", "foo", "bar",
    "quick", "brown", "fox", "jumps", "over", "lazy", "dog"
  };

  // Sentence endings
  const vector<string> endings = {".", "!", "?", ".\"", "!'", "?\""};

  uniform_int_distribution<int> blankDist(0, 5);  // ~17% blank lines
  uniform_int_distribution<int> wordCount(2, 6);
  uniform_int_distribution<int> sentenceCount(1, 3);
  uniform_int_distribution<size_t> wordDist(0, words.size() - 1);
  uniform_int_distribution<size_t> endingDist(0, endings.size() - 1);

  for (int i = 0; i < numLines; i++) {
    if (blankDist(rng) == 0) {
      // Blank line
      test.lines.push_back("");
    } else {
      // Content line with sentences
      string line;
      int numSentences = sentenceCount(rng);

      for (int s = 0; s < numSentences; s++) {
        if (s > 0) line += "  ";  // Two spaces between sentences

        int numWords = wordCount(rng);
        for (int w = 0; w < numWords; w++) {
          if (w > 0) line += " ";
          string word = words[wordDist(rng)];
          if (w == 0) {
            // Capitalize first letter
            word[0] = static_cast<char>(toupper(word[0]));
          }
          line += word;
        }
        line += endings[endingDist(rng)];
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
  uniform_int_distribution<int> lineDist(0, numLines - 1);
  test.cursorLine = lineDist(rng);

  int lineLen = static_cast<int>(test.lines[test.cursorLine].size());
  if (lineLen > 0) {
    uniform_int_distribution<int> colDist(0, lineLen - 1);
    test.cursorCol = colDist(rng);
  } else {
    test.cursorCol = 0;
  }

  return test;
}

// =============================================================================
// Random Buffer Tests - ) motion
// =============================================================================

TEST_F(SentenceMotionsTest, ForwardSentence_RandomBuffer) {
  mt19937 rng(42);
  const int NUM_ITERATIONS = 100;

  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    uniform_int_distribution<int> linesDist(3, 10);
    auto test = generateRandomSentenceBuffer(rng, linesDist(rng));

    Position start(test.cursorLine, test.cursorCol);
    Position ours = simulateMotions(start, ")", test.lines);
    Position expected = neovimMotion(test.lines, test.cursorLine, test.cursorCol, ")");

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
  mt19937 rng(123);
  const int NUM_ITERATIONS = 100;

  int passed = 0;
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    uniform_int_distribution<int> linesDist(3, 10);
    auto test = generateRandomSentenceBuffer(rng, linesDist(rng));

    Position start(test.cursorLine, test.cursorCol);
    Position ours = simulateMotions(start, "(", test.lines);
    Position expected = neovimMotion(test.lines, test.cursorLine, test.cursorCol, "(");

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
  Position start(2, 15);  // End of "Second sentence."
  Position ours = simulateMotions(start, ")", lines);
  Position expected = neovimMotion(lines, 2, 15, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, BackwardSentence_AtStartOfBuffer) {
  Lines lines = {"First sentence.", "", "Second sentence."};

  // From first position, ( should stay at first position
  Position start(0, 0);
  Position ours = simulateMotions(start, "(", lines);
  Position expected = neovimMotion(lines, 0, 0, "(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, ForwardSentence_OnBlankLine) {
  Lines lines = {"First.", "", "", "Second."};

  // From blank line, ) should go to next sentence start
  Position start(1, 0);
  Position ours = simulateMotions(start, ")", lines);
  Position expected = neovimMotion(lines, 1, 0, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, DISABLED_BackwardSentence_OnBlankLine) {
  Lines lines = {"First.", "", "", "Second."};

  // From blank line, ( should go to previous sentence start
  Position start(2, 0);
  Position ours = simulateMotions(start, "(", lines);
  Position expected = neovimMotion(lines, 2, 0, "(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, ForwardSentence_MultipleSentencesOnLine) {
  Lines lines = {"First. Second. Third."};

  // From start, ) should go to "Second"
  Position start(0, 0);
  Position ours = simulateMotions(start, ")", lines);
  Position expected = neovimMotion(lines, 0, 0, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, DISABLED_BackwardSentence_MultipleSentencesOnLine) {
  Lines lines = {"First. Second. Third."};

  // From "Third", ( should go to "Second"
  Position start(0, 15);  // At "Third"
  Position ours = simulateMotions(start, "(", lines);
  Position expected = neovimMotion(lines, 0, 15, "(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, ForwardSentence_WithClosers) {
  Lines lines = {"\"Hello!\" she said."};

  // The ! is sentence end, " is closer
  Position start(0, 0);
  Position ours = simulateMotions(start, ")", lines);
  Position expected = neovimMotion(lines, 0, 0, ")");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, CountedMotion_2Forward) {
  Lines lines = {"First. Second. Third."};

  Position start(0, 0);
  Position ours = simulateMotions(start, "2)", lines);
  Position expected = neovimMotion(lines, 0, 0, "2)");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}

TEST_F(SentenceMotionsTest, DISABLED_CountedMotion_2Backward) {
  Lines lines = {"First. Second. Third."};

  Position start(0, 15);  // At "Third"
  Position ours = simulateMotions(start, "2(", lines);
  Position expected = neovimMotion(lines, 0, 15, "2(");

  EXPECT_EQ(ours.line, expected.line);
  EXPECT_EQ(ours.col, expected.col);
}
