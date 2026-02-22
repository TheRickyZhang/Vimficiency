// tests/CompositionOptimizer/DiffStateTest.cpp
//
// Tests for DiffState computation used in CompositionOptimizer.
// Manual edge case tests + randomized invariant validation.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="*DiffState*"

#include <gtest/gtest.h>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "VimTypes/Lines.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

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

// Validate structural invariants on a diff set
static void validateInvariants(
    const vector<DiffState>& diffs, const Lines& initial, const Lines& goal) {

  string startFlat = initial.flatten();
  string goalFlat = goal.flatten();

  // Diffs must be in document order and non-overlapping
  for (size_t i = 0; i + 1 < diffs.size(); i++) {
    const auto& a = diffs[i];
    const auto& b = diffs[i + 1];
    EXPECT_TRUE(a.endPos.line < b.beginPos.line ||
                (a.endPos.line == b.beginPos.line && a.endPos.col <= b.beginPos.col))
        << "diff[" << i << "] endPos (" << a.endPos.line << "," << a.endPos.col
        << ") overlaps diff[" << (i + 1) << "] beginPos ("
        << b.beginPos.line << "," << b.beginPos.col << ")";
  }

  for (size_t i = 0; i < diffs.size(); i++) {
    const auto& d = diffs[i];

    // Type flags are mutually exclusive (or all false for empty diff)
    int typeCount = d.isPureInsertion() + d.isPureDeletion() + d.isReplacement();
    EXPECT_LE(typeCount, 1) << "diff[" << i << "] has multiple type flags";

    // Non-empty diff must have at least one type
    if (!d.deletedText.empty() || !d.insertedText.empty()) {
      EXPECT_EQ(typeCount, 1) << "diff[" << i << "] is non-empty but has no type flag";
    }

    // Pure insertion: beginPos == endPos
    if (d.isPureInsertion()) {
      EXPECT_EQ(d.beginPos, d.endPos) << "pure insertion diff[" << i << "] has non-empty range";
    }

    // Deletion/replacement: beginPos != endPos
    if (d.isPureDeletion() || d.isReplacement()) {
      EXPECT_NE(d.beginPos, d.endPos) << "deletion/replacement diff[" << i << "] has empty range";
    }

    // deletedText must match the original content at [beginPos, endPos)
    if (d.hasDeletedContent()) {
      // Convert beginPos to flat index
      int flatBegin = 0;
      for (int l = 0; l < d.beginPos.line; l++)
        flatBegin += static_cast<int>(initial[l].size()) + 1; // +1 for \n
      flatBegin += d.beginPos.col;

      string actual = startFlat.substr(flatBegin, d.deletedText.size());
      EXPECT_EQ(actual, d.deletedText)
          << "diff[" << i << "] deletedText doesn't match original at ("
          << d.beginPos.line << "," << d.beginPos.col << ")";
    }
  }
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

// =============================================================================
// Randomized Tests
// =============================================================================

namespace {

// Apply random edits to a line-based buffer. Edits operate per-line to avoid
// inserting/removing newlines, which keeps line structure predictable.
Lines randomlyEdit(const Lines& initial) {
  Lines result = initial;

  int numEdits = RandomGen::range(1, 3);
  for (int e = 0; e < numEdits; e++) {
    int line = RandomGen::range(0, static_cast<int>(result.size()) - 1);
    string& s = result[line];
    int len = static_cast<int>(s.size());

    int editType = RandomGen::range(0, 2);
    if (editType == 0) {
      // Insertion
      int pos = RandomGen::range(0, len);
      int insertLen = RandomGen::range(1, 5);
      string ins;
      for (int i = 0; i < insertLen; i++)
        ins += RandomGen::pick<string_view>({{80, CharPools::LETTERS}, {20, CharPools::SPACE}});
      s.insert(pos, ins);
    } else if (editType == 1 && len > 0) {
      // Deletion
      int a = RandomGen::range(0, len - 1);
      int delLen = min(RandomGen::range(1, 5), len - a);
      s.erase(a, delLen);
    } else if (len > 0) {
      // Replacement
      int a = RandomGen::range(0, len - 1);
      int repLen = min(RandomGen::range(1, 5), len - a);
      string rep;
      for (int i = 0; i < repLen; i++)
        rep += RandomGen::pick<string_view>({{80, CharPools::LETTERS}, {20, CharPools::SPACE}});
      s.replace(a, repLen, rep);
    }
  }

  return result;
}

} // namespace

TEST(DiffStateTest, Random_SingleLine_RoundTrip) {
  constexpr int NUM_ITERATIONS = 100;
  RandomGen::seed(42);

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines initial = {randomLine(RandomGen::range(5, 30))};
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal)
        << "Round-trip failed: '" << initial.flatten()
        << "' -> '" << goal.flatten() << "'";
    validateInvariants(diffs, initial, goal);
  }
}

TEST(DiffStateTest, Random_MultiLine_RoundTrip) {
  constexpr int NUM_ITERATIONS = 100;
  RandomGen::seed(43);

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int numLines = RandomGen::range(2, 6);
    Lines initial = randomLines(numLines, 3, 15);
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal)
        << "Round-trip failed on multi-line iter=" << iter;
    validateInvariants(diffs, initial, goal);
  }
}

TEST(DiffStateTest, Random_CodeLikeBuffers) {
  constexpr int NUM_ITERATIONS = 50;
  RandomGen::seed(44);

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines initial = randomCodeBuffer(RandomGen::range(3, 8), 15);
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);
    EXPECT_EQ(Myers::applyAllDiffState(diffs, initial), goal)
        << "Round-trip failed on code-like iter=" << iter;
    validateInvariants(diffs, initial, goal);
  }
}

TEST(DiffStateTest, Random_NoChange_NoDiffs) {
  constexpr int NUM_ITERATIONS = 50;
  RandomGen::seed(45);

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    Lines lines = randomLines(RandomGen::range(1, 4), 3, 15);
    auto diffs = Myers::calculate(lines, lines);
    EXPECT_EQ(diffs.size(), 0) << "Identical buffers should produce no diffs";
  }
}

// =============================================================================
// Sequential Application Test
// =============================================================================

namespace {

// Convert (line, col) to flat index in a Lines buffer
static int posToFlat(const Position& pos, const Lines& lines) {
  int idx = 0;
  for (int i = 0; i < pos.line && i < static_cast<int>(lines.size()); i++) {
    idx += static_cast<int>(lines[i].size()) + 1;
  }
  idx += pos.col;
  return idx;
}

// Convert flat index to (line, col) in a Lines buffer
static Position flatToPos(int flatIdx, const Lines& lines) {
  int remaining = flatIdx;
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (remaining <= lineLen) {
      return Position(i, remaining);
    }
    remaining -= lineLen + 1;
  }
  int lastLine = static_cast<int>(lines.size()) - 1;
  return Position(lastLine, static_cast<int>(lines[lastLine].size()));
}

// Apply diffs one at a time with position adjustment (mirrors calculateLinesAfterDiffs)
Lines applySequentially(vector<DiffState> diffs, const Lines& initialLines) {
  Lines current = initialLines;
  int cumulativeOffset = 0;

  for (auto& diff : diffs) {
    if (cumulativeOffset != 0) {
      auto adjustPos = [&](const Position& pos) -> Position {
        int flatIdx = posToFlat(pos, initialLines);
        flatIdx += cumulativeOffset;
        return flatToPos(flatIdx, current);
      };

      diff.beginPos = adjustPos(diff.beginPos);
      if (diff.hasDeletedContent()) {
        diff.endPos = adjustPos(diff.endPos);
      } else {
        diff.endPos = diff.beginPos;
      }
    }

    current = Myers::applyDiffState(diff, current);

    cumulativeOffset += static_cast<int>(diff.insertedText.size())
                      - static_cast<int>(diff.deletedText.size());
  }

  return current;
}

} // namespace

TEST(DiffStateTest, Random_SequentialApplication) {
  constexpr int NUM_ITERATIONS = 200;
  RandomGen::seed(46);

  for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
    int numLines = RandomGen::range(1, 6);
    Lines initial = randomLines(numLines, 3, 15);
    Lines goal = randomlyEdit(initial);
    if (initial == goal) continue;

    auto diffs = Myers::calculate(initial, goal);

    // applyAllDiffState is known-correct (applies in reverse using original positions)
    Lines expected = Myers::applyAllDiffState(diffs, initial);
    ASSERT_EQ(expected, goal) << "applyAllDiffState sanity check failed, iter=" << iter;

    // Sequential application with adjustment must produce the same result
    Lines sequential = applySequentially(diffs, initial);
    EXPECT_EQ(sequential, expected)
        << "Sequential application failed, iter=" << iter
        << "\ninitial: " << initial.flatten()
        << "\ngoal: " << goal.flatten()
        << "\nsequential: " << sequential.flatten()
        << "\ndiffs: " << diffs.size();
  }
}
