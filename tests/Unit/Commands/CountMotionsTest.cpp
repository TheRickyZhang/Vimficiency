// tests/Unit/Commands/CountMotionsTest.cpp
//
// BufferIndex internals, CountableMovementPair tables, and hard "this count
// capability exists" backstops for the NavOptimizer. The full ranked
// recommendation menus for these scenarios are snapshotted in
// tests/Approval/CountMotionsApprovalTest.cpp; the assertions here only guard
// against a capability being silently dropped.

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "Optimizer/NavOptimizer/BufferIndex.h"
#include "Optimizer/NavOptimizer/CountableMovementPair.h"
#include "Types/NavContext.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Boundary/NavBoundary.h"
#include "Utils/TestUtils.h"

using namespace std;

namespace {

bool containsPair(
    const vector<CountableMovementPair>& pairs,
    const char* forward,
    const char* backward,
    LandingType type) {
  return any_of(pairs.begin(), pairs.end(), [&](const CountableMovementPair& pair) {
    return pair.forward.seq.view() == forward &&
           pair.backward.seq.view() == backward &&
           pair.type == type;
  });
}

}  // namespace

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

TEST(BufferIndexTest, SmallWordLandingsIncludePunctuationRuns) {
  Lines lines = {"aa.,bb cc"};
  BufferIndex idx(lines);

  const auto& begins = idx.getPositions(LandingType::WordBegin);
  EXPECT_NE(find(begins.begin(), begins.end(), Pos(0, 2)), begins.end())
      << "small-word begins must include punctuation runs";

  const auto& ends = idx.getPositions(LandingType::WordEnd);
  EXPECT_NE(find(ends.begin(), ends.end(), Pos(0, 3)), ends.end())
      << "small-word ends must include punctuation runs";
}

// =============================================================================
// Optimizer Integration Tests for Count Motions
// =============================================================================

class CountMotionsOptimizerTest : public ::testing::Test {
protected:
  static Lines wordLine;
  static NavContext navContext;

  static void SetUpTestSuite() {
    // "one two three four five six seven eight"
    wordLine = {"one two three four five six seven eight"};
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

TEST_F(CountMotionsOptimizerTest, SmallCount_NotEmitted) {
  // Count of 1 should not be emitted (just use the motion directly)
  CursorPos start(0, 0);
  CursorPos end(0, 4);  // "two" - just one w away
  string userSeq = "w";

  auto results = runOptimizer(wordLine, start, end, userSeq);

  EXPECT_FALSE(any_of(results.begin(), results.end(), [](const auto& result) {
    return result.getSequence() == "1w";
  })) << "Should not emit 1w, just w";
}

// =============================================================================
// CountableMovementPair Structure Tests
// =============================================================================

TEST(CountableMovementPairTest, LineMotionsContainExpectedPairs) {
  EXPECT_TRUE(containsPair(
      COUNT_SEARCHABLE_MOTIONS_LINE, "w", "b", LandingType::WordBegin))
      << "Missing w/b WordBegin pair";
  EXPECT_TRUE(containsPair(
      COUNT_SEARCHABLE_MOTIONS_LINE, "e", "ge", LandingType::WordEnd))
      << "Missing e/ge WordEnd pair";
  EXPECT_TRUE(containsPair(
      COUNT_SEARCHABLE_MOTIONS_LINE, "W", "B", LandingType::BigWordBegin))
      << "Missing W/B BigWordBegin pair";
  EXPECT_TRUE(containsPair(
      COUNT_SEARCHABLE_MOTIONS_LINE, "E", "gE", LandingType::BigWordEnd))
      << "Missing E/gE BigWordEnd pair";
}

TEST(CountableMovementPairTest, GlobalMotionsContainParagraph) {
  EXPECT_TRUE(containsPair(
      COUNT_SEARCHABLE_MOTIONS_GLOBAL, "}", "{", LandingType::Paragraph))
      << "Missing }/{  Paragraph pair";
}
