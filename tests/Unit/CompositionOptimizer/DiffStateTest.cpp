// tests/Unit/CompositionOptimizer/DiffStateTest.cpp
//
// Tests for DiffState computation used in CompositionOptimizer.
// Manual edge case tests. Generated invariant coverage lives in Property/.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="*DiffState*"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>

#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/CompositionOptimizer/Tree.h"
#include "Optimizer/CompositionOptimizer/TreeDiff.h"
#include "Keyboard/Config.h"
#include "Types/Lines.h"

using namespace std;

using TreeNode = TreeDiff::Tree::Node;

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

static void expectTreeRoundTrip(const Lines& start, const Lines& end) {
  auto diffs = TreeDiff::calculate(start, end, Config::uniform());
  EXPECT_EQ(Myers::applyAllDiffState(diffs, start), end);
}

static string_view treeSpan(
    const TreeDiff::Tree& tree,
    const TreeNode& node) {
  const auto [begin, end] = node.text;
  return string_view(tree.text).substr(
      begin,
      end - begin);
}

static void expectFormalRefinement(const TreeDiff::Tree& tree) {
  for (int level = 0; level < TreeDiff::LEVEL_COUNT; level++) {
    for (const TreeNode& node : tree[level]) {
      EXPECT_LT(node.text.begin, node.text.end);
    }
  }

  if (tree.text.empty()) {
    for (int level = 0; level < TreeDiff::LEVEL_COUNT; level++) {
      EXPECT_TRUE(tree[level].empty());
    }
    return;
  }

  ASSERT_EQ(tree[TreeDiff::Level::Root].size(), 1u);
  EXPECT_EQ(tree[TreeDiff::Level::Root][0].text.begin, 0);
  EXPECT_EQ(tree[TreeDiff::Level::Root][0].text.end, ssize(tree.text));

  for (int level = 0; level < TreeDiff::LEVEL_COUNT - 1; level++) {
    const auto& children = tree[level + 1];
    for (const TreeNode& parent : tree[level]) {
      const auto [childBegin, childEnd] = parent.children;
      ASSERT_LT(childBegin, childEnd);
      ASSERT_GE(childBegin, 0);
      ASSERT_LE(childEnd, ssize(children));

      EXPECT_EQ(children[childBegin].text.begin, parent.text.begin);
      EXPECT_EQ(children[childEnd - 1].text.end, parent.text.end);
      for (int child = childBegin + 1; child < childEnd; child++) {
        EXPECT_EQ(children[child - 1].text.end, children[child].text.begin);
      }
    }
  }
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

TEST(TreeDiffTest, BuildTree_ParagraphAndLineRanges) {
  TreeDiff::Tree tree({"one", "", "two"});
  expectFormalRefinement(tree);

  ASSERT_EQ(tree[TreeDiff::Level::Root].size(), 1u);
  EXPECT_EQ(tree.text, "one\n\ntwo");

  const TreeNode& root = tree[TreeDiff::Level::Root][0];
  const auto [rootChildBegin, rootChildEnd] = root.children;
  EXPECT_EQ(rootChildBegin, 0);
  EXPECT_EQ(rootChildEnd, 2);

  const auto& paragraphs = tree[TreeDiff::Level::Paragraph];
  ASSERT_EQ(paragraphs.size(), 2u);
  EXPECT_EQ(treeSpan(tree, paragraphs[0]), "one\n\n");
  EXPECT_EQ(treeSpan(tree, paragraphs[1]), "two");
  const auto [firstParaChildBegin, firstParaChildEnd] = paragraphs[0].children;
  const auto [secondParaChildBegin, secondParaChildEnd] = paragraphs[1].children;
  EXPECT_EQ(firstParaChildBegin, 0);
  EXPECT_EQ(firstParaChildEnd, 2);
  EXPECT_EQ(secondParaChildBegin, 2);
  EXPECT_EQ(secondParaChildEnd, 3);
}

TEST(TreeDiffTest, BuildTree_WhitespaceOnlyLineSeparatesParagraphs) {
  TreeDiff::Tree tree({"one", "  ", "two"});
  expectFormalRefinement(tree);

  const auto& paragraphs = tree[TreeDiff::Level::Paragraph];
  ASSERT_EQ(paragraphs.size(), 2u);
  EXPECT_EQ(treeSpan(tree, paragraphs[0]), "one\n  \n");
  EXPECT_EQ(treeSpan(tree, paragraphs[1]), "two");

  const auto& bigWords = tree[TreeDiff::Level::BigWord];
  ASSERT_EQ(bigWords.size(), 3u);
  EXPECT_EQ(treeSpan(tree, bigWords[0]), "one\n");
  EXPECT_EQ(treeSpan(tree, bigWords[1]), "  \n");
  EXPECT_EQ(treeSpan(tree, bigWords[2]), "two");

  const auto& words = tree[TreeDiff::Level::Word];
  ASSERT_EQ(words.size(), 3u);
  EXPECT_EQ(treeSpan(tree, words[0]), "one\n");
  EXPECT_EQ(treeSpan(tree, words[1]), "  \n");
  EXPECT_EQ(treeSpan(tree, words[2]), "two");
}

TEST(TreeDiffTest, BuildTree_EmptyBufferHasNoNodes) {
  TreeDiff::Tree tree({""});
  expectFormalRefinement(tree);

  EXPECT_EQ(tree.text, "");
  EXPECT_EQ(tree[TreeDiff::Level::Root].size(), 0u);
  EXPECT_EQ(tree[TreeDiff::Level::Paragraph].size(), 0u);
  EXPECT_EQ(tree[TreeDiff::Level::Line].size(), 0u);
  EXPECT_EQ(tree[TreeDiff::Level::BigWord].size(), 0u);
  EXPECT_EQ(tree[TreeDiff::Level::Word].size(), 0u);
  EXPECT_EQ(tree[TreeDiff::Level::Char].size(), 0u);
}

TEST(TreeDiffTest, BuildTree_FinalNewlineStaysInLastLine) {
  TreeDiff::Tree tree({"a", ""});
  expectFormalRefinement(tree);

  ASSERT_EQ(tree[TreeDiff::Level::Paragraph].size(), 1u);
  ASSERT_EQ(tree[TreeDiff::Level::Line].size(), 1u);
  EXPECT_EQ(tree.text, "a\n");
  EXPECT_EQ(treeSpan(tree, tree[TreeDiff::Level::Paragraph][0]), "a\n");
  EXPECT_EQ(treeSpan(tree, tree[TreeDiff::Level::Line][0]), "a\n");

  ASSERT_EQ(tree[TreeDiff::Level::BigWord].size(), 1u);
  ASSERT_EQ(tree[TreeDiff::Level::Word].size(), 1u);
  EXPECT_EQ(treeSpan(tree, tree[TreeDiff::Level::BigWord][0]), "a\n");
  EXPECT_EQ(treeSpan(tree, tree[TreeDiff::Level::Word][0]), "a\n");
}

TEST(TreeDiffTest, BuildTree_WordsAndBigWordsCoverBlankLineSeparators) {
  TreeDiff::Tree tree({"a", "bc", "", "d"});
  expectFormalRefinement(tree);

  const auto& bigWords = tree[TreeDiff::Level::BigWord];
  ASSERT_EQ(bigWords.size(), 4u);
  EXPECT_EQ(treeSpan(tree, bigWords[0]), "a\n");
  EXPECT_EQ(treeSpan(tree, bigWords[1]), "bc\n");
  EXPECT_EQ(treeSpan(tree, bigWords[2]), "\n");
  EXPECT_EQ(treeSpan(tree, bigWords[3]), "d");

  const auto& words = tree[TreeDiff::Level::Word];
  ASSERT_EQ(words.size(), 4u);
  EXPECT_EQ(treeSpan(tree, words[0]), "a\n");
  EXPECT_EQ(treeSpan(tree, words[1]), "bc\n");
  EXPECT_EQ(treeSpan(tree, words[2]), "\n");
  EXPECT_EQ(treeSpan(tree, words[3]), "d");
}

TEST(TreeDiffTest, BuildTree_AttachesWhitespaceToPreviousUnits) {
  TreeDiff::Tree tree({"aa aa"});
  expectFormalRefinement(tree);

  const TreeNode& line = tree[TreeDiff::Level::Line][0];
  const auto& bigWords = tree[TreeDiff::Level::BigWord];
  const auto [bigWordBegin, bigWordEnd] = line.children;
  ASSERT_EQ(bigWordEnd - bigWordBegin, 2);
  EXPECT_EQ(treeSpan(tree, bigWords[bigWordBegin]), "aa ");
  EXPECT_EQ(treeSpan(tree, bigWords[bigWordBegin + 1]), "aa");

  const auto [wordBegin, wordEnd] = bigWords[bigWordBegin].children;
  const TreeNode& firstWord =
      tree[TreeDiff::Level::Word][wordBegin];
  EXPECT_EQ(treeSpan(tree, firstWord), "aa ");
  EXPECT_EQ(wordEnd - wordBegin, 1);
  const auto [charBegin, charEnd] = firstWord.children;
  EXPECT_EQ(charEnd - charBegin, 3);
}

TEST(TreeDiffTest, BuildTree_AttachesLeadingWhitespaceToFirstWord) {
  TreeDiff::Tree tree({"  aa"});
  expectFormalRefinement(tree);

  const auto& bigWords = tree[TreeDiff::Level::BigWord];
  ASSERT_EQ(bigWords.size(), 1u);
  EXPECT_EQ(treeSpan(tree, bigWords[0]), "  aa");

  const auto& words = tree[TreeDiff::Level::Word];
  ASSERT_EQ(words.size(), 1u);
  EXPECT_EQ(treeSpan(tree, words[0]), "  aa");
}

TEST(TreeDiffTest, BuildTree_AttachesMultipleWhitespaceToPreviousUnits) {
  TreeDiff::Tree tree({"aa  bb"});
  expectFormalRefinement(tree);

  const auto& bigWords = tree[TreeDiff::Level::BigWord];
  ASSERT_EQ(bigWords.size(), 2u);
  EXPECT_EQ(treeSpan(tree, bigWords[0]), "aa  ");
  EXPECT_EQ(treeSpan(tree, bigWords[1]), "bb");

  const auto& words = tree[TreeDiff::Level::Word];
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(treeSpan(tree, words[0]), "aa  ");
  EXPECT_EQ(treeSpan(tree, words[1]), "bb");
}

TEST(TreeDiffTest, BuildTree_NewlineSeparatesWordUnits) {
  TreeDiff::Tree tree({"aa", "bb"});
  expectFormalRefinement(tree);

  const auto& bigWords = tree[TreeDiff::Level::BigWord];
  ASSERT_EQ(bigWords.size(), 2u);
  EXPECT_EQ(treeSpan(tree, bigWords[0]), "aa\n");
  EXPECT_EQ(treeSpan(tree, bigWords[1]), "bb");

  const auto& words = tree[TreeDiff::Level::Word];
  ASSERT_EQ(words.size(), 2u);
  EXPECT_EQ(treeSpan(tree, words[0]), "aa\n");
  EXPECT_EQ(treeSpan(tree, words[1]), "bb");
}

TEST(TreeDiffTest, BuildTree_SplitsSmallWordsAndSymbolsInsideBigWord) {
  TreeDiff::Tree tree({"foo.bar"});
  expectFormalRefinement(tree);

  const TreeNode& bigWord = tree[TreeDiff::Level::BigWord][0];
  const auto& words = tree[TreeDiff::Level::Word];
  const auto [wordBegin, wordEnd] = bigWord.children;
  ASSERT_EQ(wordEnd - wordBegin, 3);
  EXPECT_EQ(treeSpan(tree, words[wordBegin]), "foo");
  EXPECT_EQ(treeSpan(tree, words[wordBegin + 1]), ".");
  EXPECT_EQ(treeSpan(tree, words[wordBegin + 2]), "bar");
}

TEST(TreeDiffTest, RoundTrip_MixedInsertDeleteReplace) {
  expectTreeRoundTrip(
      Lines{"alpha beta", "  gamma", "", "tail"},
      Lines{"alpha beet", "  gamma plus", "", "fin"});
}

TEST(TreeDiffTest, EmptyInitialUsesTreeInsertPath) {
  auto diffs = TreeDiff::calculate({""}, {"abc"}, Config::uniform());

  expectDiffs(diffs, {{"", "abc"}});
  EXPECT_EQ(diffs[0].beginPos, CursorPos(0, 0));
  EXPECT_EQ(diffs[0].endPos, CursorPos(0, 0));
}

TEST(TreeDiffTest, EmptyGoalUsesTreeDeletePath) {
  auto diffs = TreeDiff::calculate({"abc"}, {""}, Config::uniform());

  expectDiffs(diffs, {{"abc", ""}});
  EXPECT_EQ(diffs[0].beginPos, CursorPos(0, 0));
  EXPECT_EQ(diffs[0].endPos, CursorPos(0, 3));
}

TEST(TreeDiffTest, OpenPenaltyControlsSplitVsMerge) {
  Config config = Config::uniform();
  Lines initial{"aaa bbb ccc"};
  Lines goal{"xxx bbb yyy"};

  auto lowPenalty = TreeDiff::calculate(
      initial, goal, config, TreeDiff::CostOptions{.diffOpenPenalty = 0.0});
  EXPECT_GT(lowPenalty.size(), 1u);

  auto highPenalty = TreeDiff::calculate(
      initial, goal, config, TreeDiff::CostOptions{.diffOpenPenalty = 100.0});
  expectDiffs(highPenalty, {{"aaa bbb ccc", "xxx bbb yyy"}});
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

  array<size_t, 3> order{0, 1, 2};
  do {
    vector<DiffState> permuted;
    for (size_t idx : order) {
      permuted.push_back(diffs[idx]);
    }
    EXPECT_EQ(applySequentially(permuted, initial), goal)
        << "failed order "
        << order[0] << "," << order[1] << "," << order[2];
  } while (next_permutation(order.begin(), order.end()));
}
