// tests/Commands/CountMotionsTest.cpp
//
// Tests for count prefix motions (e.g., 3w, 5j) and BufferIndex.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="CountMotionsOptimizerTest.*"

#include <gtest/gtest.h>

#include "Optimizer/NavOptimizer/BufferIndex.h"
#include "Optimizer/NavOptimizer/CountableMovementPair.h"
#include "Types/NavContext.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Boundary/NavBoundary.h"
#include "Utils/TestUtils.h"

using namespace std;

// =============================================================================
// BufferIndex Unit Tests
// =============================================================================

// -----------------------------------------------------------------------------
// BufferIndex Edge Cases
// -----------------------------------------------------------------------------

TEST(BufferIndexTest, EmptyBuffer) {
  Lines empty = {};
  BufferIndex idx(empty);
  // Should not crash - just have empty position lists
}

// =============================================================================
// Optimizer Integration Tests for Count Motions
// =============================================================================

class CountMotionsOptimizerTest : public ::testing::Test {
protected:
  static Lines wordLine;
  static Lines multiWordLines;
  static NavContext navContext;

  static void SetUpTestSuite() {
    // "one two three four five six seven eight"
    wordLine = {"one two three four five six seven eight"};
    multiWordLines = {
      "first second third fourth",
      "alpha beta gamma delta",
      "",
      "after blank paragraph"
    };
    navContext = NavContext();
  }

  static vector<LandingResult> runOptimizer(
      const Lines& lines,
      CursorPos start,
      CursorPos end,
      const string& userSeq,
      Config config = Config::uniform()) {
    NavOptimizer opt(config);
    NavBoundary boundary;
    return opt.optimize(lines, start, end,
                        NavOptimizerParams{}
                            .withMaxResults(30)
                            .withMaxNodesPopped(20000)
                            .withMaxResultsPerEndPos(2),
                        userSeq, boundary, navContext).getResults();
  }
};

Lines CountMotionsOptimizerTest::wordLine;
Lines CountMotionsOptimizerTest::multiWordLines;
NavContext CountMotionsOptimizerTest::navContext(0, 0);

TEST_F(CountMotionsOptimizerTest, CountW_BasicForward) {
  // "one two three four five six seven eight"
  //  0   4   8     14   19   24  28    34
  // Start at "one", want to get to "five" (5th word)
  // User typed "wwww" but "4w" should be found
  CursorPos start(0, 0);
  CursorPos end(0, 19);  // "five" starts at col 19
  string userSeq = "wwww";

  auto results = runOptimizer(wordLine, start, end, userSeq);

  // Should find "4w" as an alternative
  EXPECT_TRUE(contains_all(results, {"4w"})) << "Should find count-prefixed 4w";
}

TEST_F(CountMotionsOptimizerTest, CountB_BasicBackward) {
  // "one two three four five six seven eight"
  //  0   4   8     14   19   24  28    34
  // Start at "eight", want to get to "four"
  CursorPos start(0, 34);  // "eight" starts at 34
  CursorPos end(0, 14);    // "four" starts at 14
  string userSeq = "bbbb";

  auto results = runOptimizer(wordLine, start, end, userSeq);

  // Should find count-prefixed backward motion
  EXPECT_TRUE(contains_all(results, {"4b"})) << "Should find count-prefixed 4b";
}

TEST_F(CountMotionsOptimizerTest, CountE_ForwardToWordEnd) {
  // "one two three four five six seven eight"
  //  0   4   8     14   19   24  28    34
  // Word ends: 2, 6, 12, 17, 22, 26, 32, 38
  // Start at beginning, want to get to end of "four"
  CursorPos start(0, 0);
  CursorPos end(0, 17);  // end of "four"
  string userSeq = "eeee";

  auto results = runOptimizer(wordLine, start, end, userSeq);

  // Should find "4e"
  EXPECT_TRUE(contains_all(results, {"4e"})) << "Should find count-prefixed 4e";
}

TEST_F(CountMotionsOptimizerTest, CountGe_BackwardToWordEnd) {
  // "one two three four five six seven eight"
  // Word ends: 2, 6, 12, 17, 22, 26, 32, 38
  // Start at end of "eight" (38), want to get to end of "four" (17)
  CursorPos start(0, 38);
  CursorPos end(0, 17);  // end of "four"
  string userSeq = "gegegege";

  auto results = runOptimizer(wordLine, start, end, userSeq);

  // Should find count-prefixed ge (4ge lands at 17)
  EXPECT_TRUE(contains_all(results, {"4ge"})) << "Should find count-prefixed 4ge";
}

TEST_F(CountMotionsOptimizerTest, CountW_SameLineOnly) {
  // The COUNT_SEARCHABLE_MOTIONS_LINE should only apply when on same line
  CursorPos start(0, 0);
  CursorPos end(1, 6);  // "beta" on line 1
  string userSeq = "jwww";

  auto results = runOptimizer(multiWordLines, start, end, userSeq);

  // Should still find good results, even though cross-line
  EXPECT_FALSE(results.empty());
}

TEST_F(CountMotionsOptimizerTest, CountParagraph_Global) {
  // Paragraph motions are in GLOBAL, should work across lines
  CursorPos start(0, 0);
  CursorPos end(3, 0);  // "after blank paragraph"
  string userSeq = "}}";

  auto results = runOptimizer(
    multiWordLines, start, end, userSeq
  );

  // Should find paragraph-based motions
  EXPECT_FALSE(results.empty());
}

TEST_F(CountMotionsOptimizerTest, SmallCount_NotEmitted) {
  // Count of 1 should not be emitted (just use the motion directly)
  CursorPos start(0, 0);
  CursorPos end(0, 4);  // "two" - just one w away
  string userSeq = "w";

  auto results = runOptimizer(wordLine, start, end, userSeq);

  // Should find "w", not "1w"
  bool has_1w = false;
  for (const auto& r : results) {
    if (r.getSequence() == "1w") {
      has_1w = true;
      break;
    }
  }
  EXPECT_FALSE(has_1w) << "Should not emit 1w, just w";
}

// =============================================================================
// CountableMovementPair Structure Tests
// =============================================================================

TEST(CountableMovementPairTest, LineMotionsContainExpectedPairs) {
  // Verify COUNT_SEARCHABLE_MOTIONS_LINE has correct structure
  bool hasWordBegin = false;
  bool hasWordEnd = false;
  bool hasWORDBegin = false;
  bool hasWORDEnd = false;

  for (const auto& pair : COUNT_SEARCHABLE_MOTIONS_LINE) {
    if (pair.forward.seq.view() == "w" && pair.backward.seq.view() == "b" && pair.type == LandingType::WordBegin) {
      hasWordBegin = true;
    }
    if (pair.forward.seq.view() == "e" && pair.backward.seq.view() == "ge" && pair.type == LandingType::WordEnd) {
      hasWordEnd = true;
    }
    if (pair.forward.seq.view() == "W" && pair.backward.seq.view() == "B" && pair.type == LandingType::WORDBegin) {
      hasWORDBegin = true;
    }
    if (pair.forward.seq.view() == "E" && pair.backward.seq.view() == "gE" && pair.type == LandingType::WORDEnd) {
      hasWORDEnd = true;
    }
  }

  EXPECT_TRUE(hasWordBegin) << "Missing w/b WordBegin pair";
  EXPECT_TRUE(hasWordEnd) << "Missing e/ge WordEnd pair";
  EXPECT_TRUE(hasWORDBegin) << "Missing W/B WORDBegin pair";
  EXPECT_TRUE(hasWORDEnd) << "Missing E/gE WORDEnd pair";
}

TEST(CountableMovementPairTest, GlobalMotionsContainParagraphAndSentence) {
  bool hasParagraph = false;
  bool hasSentence = false;

  for (const auto& pair : COUNT_SEARCHABLE_MOTIONS_GLOBAL) {
    if (pair.forward.seq.view() == "}" && pair.backward.seq.view() == "{" && pair.type == LandingType::Paragraph) {
      hasParagraph = true;
    }
    if (pair.forward.seq.view() == ")" && pair.backward.seq.view() == "(" && pair.type == LandingType::Sentence) {
      hasSentence = true;
    }
  }

  EXPECT_TRUE(hasParagraph) << "Missing }/{  Paragraph pair";
  EXPECT_TRUE(hasSentence) << "Missing )/( Sentence pair";
}
