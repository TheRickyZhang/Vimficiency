#include <gtest/gtest.h>

#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/EditSequenceSpan.h"
#include "Keyboard/Config.h"
#include "Boundary/NavBoundary.h"
#include "Types/Lines.h"
#include "Types/CursorPos.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

class EditSequenceSpanTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};

  CompositionTraceResult run(Lines initial, CursorPos initialPos,
                             Lines goal, CursorPos goalPos) {
    NavBoundary boundary(
        initial, CursorPos(0, 0),
        CursorPos(static_cast<int>(initial.size()) - 1,
                  static_cast<int>(initial.back().size()) + 1),
        false, false);
    CompositionOptimizer opt(config);
    return opt.optimizeWithEditSpans(
        initial, initialPos, goal, goalPos,
        CompositionOptimizerParams{}.withMaxResults(1),
        "", boundary, navContext);
  }
};

// rowsFromEditSpans: the helper itself, exercised on synthetic inputs.

TEST(RowsFromEditSpans, EmptySequence) {
  auto rows = rowsFromEditSpans("", {});
  EXPECT_TRUE(rows.empty());
}

TEST(RowsFromEditSpans, NoSpansNonEmptySeqGivesOneRow) {
  string seq = "wwwf$";
  auto rows = rowsFromEditSpans(seq, {});
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], "wwwf$");
}

TEST(RowsFromEditSpans, SingleEditNoSurroundingNav) {
  string seq = "dw";
  EditSequenceSpan span{0, 2};
  auto rows = rowsFromEditSpans(seq, {&span, 1});
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0], "dw");
}

TEST(RowsFromEditSpans, NavThenEdit) {
  string seq = "wwde";
  EditSequenceSpan span{2, 4};
  auto rows = rowsFromEditSpans(seq, {&span, 1});
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0], "ww");
  EXPECT_EQ(rows[1], "de");
}

TEST(RowsFromEditSpans, AdjacentEditsNoEmptyNavBetween) {
  // dE then ce... — two planned edits, no navigation between them.
  string seq = "dEcei+1<Esc>";
  EditSequenceSpan spans[]{{0, 2}, {2, 12}};
  auto rows = rowsFromEditSpans(seq, {spans, 2});
  ASSERT_EQ(rows.size(), 2u) << "adjacent edits must not introduce empty nav row";
  EXPECT_EQ(rows[0], "dE");
  EXPECT_EQ(rows[1], "cei+1<Esc>");
}

TEST(RowsFromEditSpans, NavEditNavEdit) {
  string seq = "wdwwwde";
  EditSequenceSpan spans[]{{1, 3}, {6, 8}};
  // Wait — second span end is 8 but seq.size() is 7. Adjust.
  spans[1] = {5, 7};
  auto rows = rowsFromEditSpans(seq, {spans, 2});
  ASSERT_EQ(rows.size(), 4u);
  EXPECT_EQ(rows[0], "w");
  EXPECT_EQ(rows[1], "dw");
  EXPECT_EQ(rows[2], "ww");
  EXPECT_EQ(rows[3], "de");
}

TEST(RowsFromEditSpans, EditThenTrailingNav) {
  string seq = "dej";
  EditSequenceSpan span{0, 2};
  auto rows = rowsFromEditSpans(seq, {&span, 1});
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0], "de");
  EXPECT_EQ(rows[1], "j");
}

TEST(RowsFromEditSpans, ZeroLengthSpansAreSkippedButGapsArePreserved) {
  // Zero-length spans don't emit edit rows (no empty rows ever) but they DO
  // partition the surrounding gap. So {{0,0},{1,1},{2,2}} on "abc" yields
  // three nav rows ["a","b","c"], not one. This is the correct behavior:
  // zero-length spans don't merge gaps, they just don't show themselves.
  string seq = "abc";
  EditSequenceSpan spans[]{{0, 0}, {1, 1}, {2, 2}};
  auto rows = rowsFromEditSpans(seq, {spans, 3});
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0], "a");
  EXPECT_EQ(rows[1], "b");
  EXPECT_EQ(rows[2], "c");
}

TEST(RowsFromEditSpans, ZeroLengthSpanInsideRealGapEmitsNoEditRow) {
  // The real invariant: a zero-length span in the middle of a gap doesn't
  // produce an empty edit row.
  string seq = "abcde";
  EditSequenceSpan span{2, 2};  // empty span at offset 2
  auto rows = rowsFromEditSpans(seq, {&span, 1});
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0], "ab");
  EXPECT_EQ(rows[1], "cde");
  for (const auto& row : rows) EXPECT_FALSE(row.empty());
}

// optimizeWithEditSpans: the traced search produces aligned spans for every
// result it emits. We don't assert specific byte values (those depend on
// optimizer scoring) — we assert the structural invariants.

TEST_F(EditSequenceSpanTest, PureMotionEmitsNoSpans) {
  Lines lines{Line("foo bar baz")};
  auto traced = run(lines, {0, 0}, lines, {0, 4});
  ASSERT_FALSE(traced.result.getResults().empty());
  EXPECT_EQ(traced.result.totalEdits(), 0);
  for (const auto& spans : traced.editSpansByResult) {
    EXPECT_TRUE(spans.empty()) << "pure motion must record no edit spans";
  }
}

TEST_F(EditSequenceSpanTest, EveryResultGetsOneSpanPerEdit) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto traced = run(initial, {0, 0}, goal, {0, 10});
  ASSERT_FALSE(traced.result.getResults().empty());

  const int totalEdits = traced.result.totalEdits();
  ASSERT_GT(totalEdits, 0);
  ASSERT_EQ(traced.editSpansByResult.size(),
            traced.result.getResults().size());

  for (size_t i = 0; i < traced.editSpansByResult.size(); ++i) {
    const auto& spans = traced.editSpansByResult[i];
    const auto& seq = traced.result.getResults()[i].getSequence().view();
    ASSERT_EQ(static_cast<int>(spans.size()), totalEdits)
        << "result " << i << " span count must match totalEdits";
    for (const auto& span : spans) {
      EXPECT_LE(span.beginByte, span.endByte) << "span must be ordered";
      EXPECT_LE(span.endByte, seq.size()) << "span must lie inside seq";
    }
  }
}

TEST_F(EditSequenceSpanTest, RowsFromTracedResultProduceNonEmptyRows) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto traced = run(initial, {0, 0}, goal, {0, 10});

  ASSERT_FALSE(traced.result.getResults().empty());
  const auto& seq = traced.result.getResults()[0].getSequence().view();
  const auto& spans = traced.editSpansByResult[0];

  auto rows = rowsFromEditSpans(seq, spans);
  ASSERT_FALSE(rows.empty());
  size_t totalLen = 0;
  for (const auto& row : rows) {
    EXPECT_FALSE(row.empty()) << "rowsFromEditSpans must not emit empty rows";
    totalLen += row.size();
  }
  EXPECT_EQ(totalLen, seq.size())
      << "concatenated rows must reconstruct the original sequence";
}

TEST_F(EditSequenceSpanTest, OptimizePathIsByteIdenticalToTraced) {
  // Sanity: the trace policy must not influence search ordering. The optimal
  // sequence emitted by both paths should be identical, and equal cost.
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  NavBoundary boundary(
      initial, CursorPos(0, 0),
      CursorPos(0, static_cast<int>(initial[0].size()) + 1), false, false);
  CompositionOptimizer opt(config);

  auto plain = opt.optimize(initial, {0, 0}, goal, {0, 10},
                             CompositionOptimizerParams{}.withMaxResults(3),
                             "", boundary, navContext);
  auto traced = opt.optimizeWithEditSpans(
      initial, {0, 0}, goal, {0, 10},
      CompositionOptimizerParams{}.withMaxResults(3),
      "", boundary, navContext);

  ASSERT_EQ(plain.getResults().size(), traced.result.getResults().size());
  for (size_t i = 0; i < plain.getResults().size(); ++i) {
    EXPECT_EQ(plain.getResults()[i].getSequence().view(),
              traced.result.getResults()[i].getSequence().view())
        << "result " << i << " sequence must match between traced/untraced";
    EXPECT_DOUBLE_EQ(plain.getResults()[i].getCost(),
                     traced.result.getResults()[i].getCost())
        << "result " << i << " cost must match between traced/untraced";
  }
}

}  // namespace
