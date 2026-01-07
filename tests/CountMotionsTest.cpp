#include <gtest/gtest.h>

#include "Optimizer/BufferIndex.h"
#include "Keyboard/MotionToKeys.h"
#include "Editor/NavContext.h"
#include "Editor/Motion.h"
#include "Optimizer/MovementOptimizer.h"
#include "Optimizer/ImpliedExclusions.h"
#include "State/MotionState.h"
#include "TestUtils.h"

using namespace std;

// =============================================================================
// BufferIndex Unit Tests
// =============================================================================

class BufferIndexTest : public ::testing::Test {
protected:
  // Simple test buffer: "one two three four five"
  vector<string> singleLine = {"one two three four five"};

  // Multi-word lines
  vector<string> multiLine = {
    "first second third",
    "alpha beta gamma",
    "",
    "after blank line"
  };

  // Code-like content
  vector<string> codeLike = {
    "int main() {",
    "  return 0;",
    "}"
  };
};

// -----------------------------------------------------------------------------
// Word Begin (w/b) Tests
// -----------------------------------------------------------------------------

TEST_F(BufferIndexTest, WordBegin_SingleLine_Forward) {
  BufferIndex idx(singleLine);
  Position start(0, 0);  // at 'o' of "one"
  Position goal(0, 18);  // at 'f' of "five"

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::WordBegin, start, goal);

  // Words start at: 0 (one), 4 (two), 8 (three), 14 (four), 19 (five)
  // From 0 to 18: nearest word starts are at 14 (undershoot) and 19 (overshoot)
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());

  if (overshoot.valid()) {
    EXPECT_EQ(overshoot.pos.col, 19);  // "five" starts at col 19
  }
}

TEST_F(BufferIndexTest, WordBegin_SingleLine_Backward) {
  BufferIndex idx(singleLine);
  Position start(0, 19);  // at 'f' of "five"
  Position goal(0, 4);    // at 't' of "two"

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::WordBegin, start, goal);

  // Going backward from 19 to 4
  // Word starts: 19, 14, 8, 4, 0
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());
}

TEST_F(BufferIndexTest, WordBegin_MultiLine) {
  BufferIndex idx(multiLine);
  Position start(0, 0);
  Position goal(1, 6);  // "beta" on line 1

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::WordBegin, start, goal);

  // Should find positions across lines
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());
}

// -----------------------------------------------------------------------------
// Word End (e/ge) Tests
// -----------------------------------------------------------------------------

TEST_F(BufferIndexTest, WordEnd_SingleLine_Forward) {
  BufferIndex idx(singleLine);
  Position start(0, 0);
  Position goal(0, 17);  // near end of "four"

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::WordEnd, start, goal);

  // Word ends at: 2 (one), 6 (two), 12 (three), 17 (four), 22 (five)
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());
}

// -----------------------------------------------------------------------------
// WORD Begin (W/B) Tests
// -----------------------------------------------------------------------------

TEST_F(BufferIndexTest, WORDBegin_CodeLike) {
  BufferIndex idx(codeLike);
  Position start(0, 0);
  Position goal(0, 11);  // at '{'

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::WORDBegin, start, goal);

  // WORD starts: 0 (int), 4 (main()), 11 ({)
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());
}

// -----------------------------------------------------------------------------
// Paragraph Tests
// -----------------------------------------------------------------------------

TEST_F(BufferIndexTest, Paragraph_AcrossBlankLines) {
  BufferIndex idx(multiLine);
  Position start(0, 0);
  Position goal(3, 0);  // "after blank line"

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::Paragraph, start, goal);

  // Paragraphs at: line 0, line 2 (empty), line 3
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());
}

// -----------------------------------------------------------------------------
// Edge Cases
// -----------------------------------------------------------------------------

TEST_F(BufferIndexTest, EmptyBuffer) {
  vector<string> empty = {};
  BufferIndex idx(empty);
  // Should not crash - just have empty position lists
}

TEST_F(BufferIndexTest, SingleCharLine) {
  vector<string> single = {"x y z"};
  BufferIndex idx(single);
  Position start(0, 0);
  Position goal(0, 4);  // 'z'

  auto [undershoot, overshoot] = idx.getTwoClosest(LandingType::WordBegin, start, goal);
  // Word starts at 0, 2, 4
  EXPECT_TRUE(undershoot.valid() || overshoot.valid());
}

// =============================================================================
// Optimizer Integration Tests for Count Motions
// =============================================================================

class CountMotionsOptimizerTest : public ::testing::Test {
protected:
  static vector<string> wordLine;
  static vector<string> multiWordLines;
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

    navContext = NavContext(39, 19);
  }

  static vector<Result> runOptimizer(
      const vector<string>& lines,
      Position start,
      Position end,
      const string& userSeq,
      const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS,
      Config config = Config::uniform()) {
    MovementOptimizer opt(config);
    ImpliedExclusions impliedExclusions(false, false);
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        SearchParams(30, 2e4, 1.0, 2.0), impliedExclusions, allowedMotions);
  }
};

vector<string> CountMotionsOptimizerTest::wordLine;
vector<string> CountMotionsOptimizerTest::multiWordLines;
NavContext CountMotionsOptimizerTest::navContext(0, 0);

TEST_F(CountMotionsOptimizerTest, CountW_BasicForward) {
  // "one two three four five six seven eight"
  //  0   4   8     14   19   24  28    34
  // Start at "one", want to get to "five" (5th word)
  // User typed "wwww" but "4w" should be found
  Position start(0, 0);
  Position end(0, 19);  // "five" starts at col 19
  string userSeq = "wwww";

  auto results = runOptimizer(wordLine, start, end, userSeq);
  printResults(results);

  // Should find "4w" as an alternative
  EXPECT_TRUE(contains_all(results, {"4w"})) << "Should find count-prefixed 4w";
}

TEST_F(CountMotionsOptimizerTest, CountB_BasicBackward) {
  // "one two three four five six seven eight"
  //  0   4   8     14   19   24  28    34
  // Start at "eight", want to get to "four"
  Position start(0, 34);  // "eight" starts at 34
  Position end(0, 14);    // "four" starts at 14
  string userSeq = "bbbb";

  auto results = runOptimizer(wordLine, start, end, userSeq);
  printResults(results);

  // Should find count-prefixed backward motion
  EXPECT_TRUE(contains_all(results, {"4b"})) << "Should find count-prefixed 4b";
}

TEST_F(CountMotionsOptimizerTest, CountE_ForwardToWordEnd) {
  // "one two three four five six seven eight"
  //  0   4   8     14   19   24  28    34
  // Word ends: 2, 6, 12, 17, 22, 26, 32, 38
  // Start at beginning, want to get to end of "four"
  Position start(0, 0);
  Position end(0, 17);  // end of "four"
  string userSeq = "eeee";

  auto results = runOptimizer(wordLine, start, end, userSeq);
  printResults(results);

  // Should find "4e"
  EXPECT_TRUE(contains_all(results, {"4e"})) << "Should find count-prefixed 4e";
}

TEST_F(CountMotionsOptimizerTest, CountGe_BackwardToWordEnd) {
  // "one two three four five six seven eight"
  // Word ends: 2, 6, 12, 17, 22, 26, 32, 38
  // Start at end of "eight" (38), want to get to end of "four" (17)
  Position start(0, 38);
  Position end(0, 17);  // end of "four"
  string userSeq = "gegegege";

  auto results = runOptimizer(wordLine, start, end, userSeq);
  printResults(results);

  // Should find count-prefixed ge (4ge lands at 17)
  EXPECT_TRUE(contains_all(results, {"4ge"})) << "Should find count-prefixed 4ge";
}

TEST_F(CountMotionsOptimizerTest, CountW_SameLineOnly) {
  // The COUNT_SEARCHABLE_MOTIONS_LINE should only apply when on same line
  Position start(0, 0);
  Position end(1, 6);  // "beta" on line 1
  string userSeq = "jwww";

  auto results = runOptimizer(multiWordLines, start, end, userSeq);
  printResults(results);

  // Should still find good results, even though cross-line
  EXPECT_FALSE(results.empty());
}

TEST_F(CountMotionsOptimizerTest, CountParagraph_Global) {
  // Paragraph motions are in GLOBAL, should work across lines
  Position start(0, 0);
  Position end(3, 0);  // "after blank paragraph"
  string userSeq = "}}";

  auto results = runOptimizer(
    multiWordLines, start, end, userSeq,
    getSlicedMotionToKeys({"j", "k", "{", "}"})
  );
  printResults(results);

  // Should find paragraph-based motions
  EXPECT_FALSE(results.empty());
}

TEST_F(CountMotionsOptimizerTest, SmallCount_NotEmitted) {
  // Count of 1 should not be emitted (just use the motion directly)
  Position start(0, 0);
  Position end(0, 4);  // "two" - just one w away
  string userSeq = "w";

  auto results = runOptimizer(wordLine, start, end, userSeq);
  printResults(results);

  // Should find "w", not "1w"
  bool has_1w = false;
  for (const auto& r : results) {
    if (r.getSequenceString() == "1w") {
      has_1w = true;
      break;
    }
  }
  EXPECT_FALSE(has_1w) << "Should not emit 1w, just w";
}

// =============================================================================
// CountableMotionPair Structure Tests
// =============================================================================

TEST(CountableMotionPairTest, LineMotionsContainExpectedPairs) {
  // Verify COUNT_SEARCHABLE_MOTIONS_LINE has correct structure
  bool hasWordBegin = false;
  bool hasWordEnd = false;
  bool hasWORDBegin = false;
  bool hasWORDEnd = false;

  for (const auto& pair : COUNT_SEARCHABLE_MOTIONS_LINE) {
    if (pair.forward == "w" && pair.backward == "b" && pair.type == LandingType::WordBegin) {
      hasWordBegin = true;
    }
    if (pair.forward == "e" && pair.backward == "ge" && pair.type == LandingType::WordEnd) {
      hasWordEnd = true;
    }
    if (pair.forward == "W" && pair.backward == "B" && pair.type == LandingType::WORDBegin) {
      hasWORDBegin = true;
    }
    if (pair.forward == "E" && pair.backward == "gE" && pair.type == LandingType::WORDEnd) {
      hasWORDEnd = true;
    }
  }

  EXPECT_TRUE(hasWordBegin) << "Missing w/b WordBegin pair";
  EXPECT_TRUE(hasWordEnd) << "Missing e/ge WordEnd pair";
  EXPECT_TRUE(hasWORDBegin) << "Missing W/B WORDBegin pair";
  EXPECT_TRUE(hasWORDEnd) << "Missing E/gE WORDEnd pair";
}

TEST(CountableMotionPairTest, GlobalMotionsContainParagraphAndSentence) {
  bool hasParagraph = false;
  bool hasSentence = false;

  for (const auto& pair : COUNT_SEARCHABLE_MOTIONS_GLOBAL) {
    if (pair.forward == "}" && pair.backward == "{" && pair.type == LandingType::Paragraph) {
      hasParagraph = true;
    }
    if (pair.forward == ")" && pair.backward == "(" && pair.type == LandingType::Sentence) {
      hasSentence = true;
    }
  }

  EXPECT_TRUE(hasParagraph) << "Missing }/{  Paragraph pair";
  EXPECT_TRUE(hasSentence) << "Missing )/( Sentence pair";
}
