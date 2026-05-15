// tests/CompositionOptimizer/DiffStateTest.cpp
//
// Tests for DiffState computation used in CompositionOptimizer.
// Manual edge case tests + generated-property invariant validation.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="*DiffState*"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Types/Lines.h"

using namespace std;

// =============================================================================
// Helpers
// =============================================================================

static void expectDiffs(
    const vector<DiffState>& diffs,
    initializer_list<pair<const char*, const char*>> expected) {
  ASSERT_EQ(diffs.size(), expected.size()) << "diff count mismatch";
  size_t i = 0;
  for (const auto& [del, ins] : expected) {
    EXPECT_EQ(diffs[i].deletedText, del) << "diff[" << i << "].deleted";
    EXPECT_EQ(diffs[i].insertedText, ins) << "diff[" << i << "].inserted";
    i++;
  }
}

static void expectRoundTrip(const Lines& start, const Lines& end) {
  auto diffs = Myers::calculate(start, end);
  EXPECT_EQ(Myers::applyAllDiffState(diffs, start), end);
}

static DiffState makeDiff(
    const Lines& context,
    CursorPos begin,
    CursorPos end,
    string deleted,
    string inserted) {
  return DiffState(
      begin, end, std::move(deleted), std::move(inserted),
      TransformBoundary(context, begin, end));
}

// =============================================================================
// Manual Edge Case Tests
// =============================================================================

TEST(DiffStateTest, NoChange_NoDiffs) {
  auto diffs = Myers::calculate({"hello"}, {"hello"});
  EXPECT_EQ(diffs.size(), 0);
}

TEST(DiffStateTest, Substitution_OnlyChangedChars) {
  auto diffs = Myers::calculate({"the cat sat"}, {"the dog sat"});
  expectDiffs(diffs, {{"cat", "dog"}});
}

TEST(DiffStateTest, MultipleDiffs_SameLine) {
  auto diffs = Myers::calculate({"aaa bbb ccc"}, {"xxx bbb yyy"});
  expectDiffs(diffs, {{"aaa", "xxx"}, {"ccc", "yyy"}});
}

TEST(DiffStateTest, PureInsertion) {
  auto diffs = Myers::calculate({"hello"}, {"hello world"});
  expectDiffs(diffs, {{"", " world"}});
  EXPECT_TRUE(diffs[0].isPureInsertion());
  EXPECT_EQ(diffs[0].beginPos, diffs[0].endPos);
}

TEST(DiffStateTest, PureDeletion) {
  auto diffs = Myers::calculate({"hello world"}, {"hello"});
  expectDiffs(diffs, {{" world", ""}});
  EXPECT_TRUE(diffs[0].isPureDeletion());
}

TEST(DiffStateTest, MultiLine_InsertAndDeleteLine) {
  // Insert line
  auto diffs1 = Myers::calculate({"aaa", "ccc"}, {"aaa", "bbb", "ccc"});
  expectDiffs(diffs1, {{"", "bbb\n"}});

  // Delete line
  auto diffs2 = Myers::calculate({"aaa", "bbb", "ccc"}, {"aaa", "ccc"});
  expectDiffs(diffs2, {{"bbb\n", ""}});
}

TEST(DiffStateTest, MinMatch_ThresholdBehavior) {
  // 3 chars < MIN_MATCH_LENGTH=4: merged
  auto merged = Myers::calculate({"abcdef"}, {"xxcdexx"});
  expectDiffs(merged, {{"abcdef", "xxcdexx"}});

  // 4 chars >= MIN_MATCH_LENGTH=4: preserved as separator
  auto split = Myers::calculate({"abcdefgh"}, {"xxcdefxx"});
  expectDiffs(split, {{"ab", "xx"}, {"gh", "xx"}});
}

TEST(DiffStateTest, WordBoundary_OverridesMinMatch) {
  // " b " is only 3 chars but contains word boundaries, so preserved
  auto diffs = Myers::calculate({"a b c"}, {"d b e"});
  expectDiffs(diffs, {{"a", "d"}, {"c", "e"}});

  // "_b_" has underscores (NOT word boundary), so absorbed
  auto merged = Myers::calculate({"a_b_c"}, {"d_b_e"});
  expectDiffs(merged, {{"a_b_c", "d_b_e"}});
}

TEST(DiffStateTest, Position_HalfOpenSemantics) {
  auto diffs = Myers::calculate({"abcde"}, {"abXde"});
  EXPECT_EQ(diffs[0].beginPos.col, 2);
  EXPECT_EQ(diffs[0].endPos.col, 3); // Half-open: one past last

  auto ins = Myers::calculate({"hello"}, {"hello world"});
  EXPECT_EQ(ins[0].beginPos, ins[0].endPos); // Empty range for pure insertion
}

TEST(DiffStateTest, PureNewline_PreservedAsBoundary) {
  // Single newline preserved: "a\nc" -> "ab\n\nc"
  auto diffs1 = Myers::calculate({"a", "c"}, {"ab", "", "c"});
  ASSERT_EQ(diffs1.size(), 1);
  EXPECT_TRUE(diffs1[0].isPureInsertion());
  EXPECT_EQ(diffs1[0].insertedText, "b\n");

  // Newline+indent absorbed (not pure newlines)
  auto diffs2 = Myers::calculate({"a", "  c"}, {"ab", "  c"});
  ASSERT_EQ(diffs2.size(), 1);
  EXPECT_EQ(diffs2[0].insertedText, "b");

  // Each line change independent when separated by newlines
  auto diffs3 = Myers::calculate({"a", "b", "c"}, {"x", "y", "z"});
  ASSERT_EQ(diffs3.size(), 3);
  expectDiffs(diffs3, {{"a", "x"}, {"b", "y"}, {"c", "z"}});
}

TEST(DiffStateTest, ContiguousResidualDiff_CoalescesSeparatedChanges) {
  Lines from{Line("a b c")};
  Lines to{Line("x b y")};

  auto residual = DiffText::calculateContiguousResidualDiff(from, to);
  ASSERT_TRUE(residual.has_value());
  EXPECT_EQ(residual->deletedText, "a b c");
  EXPECT_EQ(residual->insertedText, "x b y");

  auto planned = Myers::calculate(from, to);
  expectDiffs(planned, {{"a", "x"}, {"c", "y"}});
}

// =============================================================================
// Sequential Application Test
// =============================================================================

namespace {

// Apply diffs one at a time in caller-specified order.
Lines applySequentially(vector<DiffState> diffs, const Lines& initialLines) {
  Lines current = initialLines;
  OriginalDiffMapper mapper;

  for (const auto& originalDiff : diffs) {
    DiffState currentDiff = mapper.mapDiffToCurrent(
        originalDiff, initialLines, current);
    current = Myers::applyDiffState(currentDiff, current);
    mapper.recordApplied(originalDiff, initialLines);
  }

  return current;
}

} // namespace

TEST(DiffStateTest, ReversedSequentialApplication) {
  Lines initial{Line("aaa"), Line("middle"), Line("tail")};
  Lines goal{Line("a"), Line("middle"), Line("tail suffix")};
  auto diffs = Myers::calculate(initial, goal);
  ASSERT_EQ(diffs.size(), 2u);

  reverse(diffs.begin(), diffs.end());

  Lines sequential = applySequentially(diffs, initial);
  EXPECT_EQ(sequential, goal);
}

TEST(DiffStateTest, OriginalDiffMapper_MapsLaterDiffAfterPriorInsertion) {
  Lines initial{Line("abc"), Line("def"), Line("ghi")};
  DiffState insertBefore = makeDiff(
      initial, CursorPos(0, 0), CursorPos(0, 0), "", "top\n");
  DiffState replaceLastLine = makeDiff(
      initial, CursorPos(2, 0), CursorPos(2, 3), "ghi", "GHI");

  OriginalDiffMapper mapper;
  Lines current = Myers::applyDiffState(insertBefore, initial);
  mapper.recordApplied(insertBefore, initial);

  DiffState mapped = mapper.mapDiffToCurrent(
      replaceLastLine, initial, current);

  EXPECT_EQ(mapped.beginPos, CursorPos(3, 0));
  EXPECT_EQ(mapped.endPos, CursorPos(3, 3));
  EXPECT_EQ(mapped.boundary.prefix(), "");
  EXPECT_EQ(mapped.boundary.suffix(), "");
  EXPECT_EQ(Myers::applyDiffState(mapped, current),
            (Lines{Line("top"), Line("abc"), Line("def"), Line("GHI")}));
}

TEST(DiffStateTest, OriginalDiffMapper_DoesNotShiftEarlierDiffAfterLaterInsertion) {
  Lines initial{Line("abc"), Line("def"), Line("ghi")};
  DiffState insertAfter = makeDiff(
      initial, CursorPos(2, 3), CursorPos(2, 3), "", "\ntail");
  DiffState replaceFirstLine = makeDiff(
      initial, CursorPos(0, 0), CursorPos(0, 3), "abc", "ABC");

  OriginalDiffMapper mapper;
  Lines current = Myers::applyDiffState(insertAfter, initial);
  mapper.recordApplied(insertAfter, initial);

  DiffState mapped = mapper.mapDiffToCurrent(
      replaceFirstLine, initial, current);

  EXPECT_EQ(mapped.beginPos, CursorPos(0, 0));
  EXPECT_EQ(mapped.endPos, CursorPos(0, 3));
  EXPECT_EQ(Myers::applyDiffState(mapped, current),
            (Lines{Line("ABC"), Line("def"), Line("ghi"), Line("tail")}));
}

TEST(DiffStateTest, OriginalDiffMapper_PermutationsOfIndependentDiffs) {
  Lines initial{Line("alpha KEEP beta KEEP gamma")};
  Lines goal{Line("ALPHA KEEP beta plus KEEP g")};
  vector<DiffState> diffs = Myers::calculate(initial, goal);
  ASSERT_EQ(diffs.size(), 3u);

  array<int, 3> order{0, 1, 2};
  do {
    vector<DiffState> permuted;
    for (int idx : order) {
      permuted.push_back(diffs[static_cast<size_t>(idx)]);
    }
    EXPECT_EQ(applySequentially(permuted, initial), goal)
        << "failed order "
        << order[0] << "," << order[1] << "," << order[2];
  } while (next_permutation(order.begin(), order.end()));
}
