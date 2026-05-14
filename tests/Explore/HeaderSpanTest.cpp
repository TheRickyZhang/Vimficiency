#include "Explore/TestHelpers.h"

using namespace std;
using ExploreTestSupport::ExploreViewTest;

namespace {

TEST_F(ExploreViewTest, PureMotionHeaderHasNoExploredRowsInitially) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto rows = view.headerRows();
  EXPECT_TRUE(rows.explored.empty());
}

TEST_F(ExploreViewTest, PureMotionHeaderShowsTypedSequenceAsOneRow) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  auto rows = view.headerRows();
  ASSERT_EQ(rows.explored.size(), 1u);
  EXPECT_EQ(rows.explored[0], "w");
  EXPECT_EQ(view.state().editSpans.size, 0)
      << "pure motion must not push any edit spans";
}

TEST_F(ExploreViewTest, AcceptBufferStateAdvancePushesOneSpanWithCorrectByteRange) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  const size_t seqBefore = view.state().seq.size();

  // Mirror the buffer-state path the explore.lua refresh uses: the user
  // typed `rB` natively, the buffer ended up matching the goal fencepost.
  ASSERT_TRUE(view.acceptBufferState(goal, CursorPos(0, 1), "rB").has_value());
  ASSERT_EQ(view.state().editSpans.size, 1);
  EXPECT_EQ(view.state().editSpans.spans[0].beginByte, seqBefore);
  EXPECT_EQ(view.state().editSpans.spans[0].endByte, view.state().seq.size());
}

TEST_F(ExploreViewTest, AcceptBufferStateNoOpDoesNotPushSpan) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  // No-op buffer state: report the *current* (pre-edit) lines. The handler
  // must accept this as a sync (advance == false) — and not push a span.
  auto outcome = view.acceptBufferState(initial, CursorPos(0, 1), "");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().editSpans.size, 0)
      << "no-op buffer sync must not push a planned-edit span";
}

TEST_F(ExploreViewTest, UndoPopsSpanInLockstepWithEditsCompleted) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(view.acceptBufferState(goal, CursorPos(0, 1), "rB").has_value());
  ASSERT_EQ(view.state().editSpans.size, 1);

  ASSERT_TRUE(view.undo().has_value());
  EXPECT_EQ(view.state().editSpans.size, 0);

  ASSERT_TRUE(view.redo().has_value());
  EXPECT_EQ(view.state().editSpans.size, 1);
}

TEST_F(ExploreViewTest, HeaderOptimalRowsAlignWithOptimalResults) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto view = makeView(initial, {0, 0}, goal, {0, 10});

  auto rows = view.headerRows();
  // One column per optimal result; each column has at least one row.
  EXPECT_FALSE(rows.optimal.empty());
  for (const auto& col : rows.optimal) {
    EXPECT_FALSE(col.empty()) << "optimal column must have at least one row";
    for (const auto& row : col) {
      EXPECT_FALSE(row.empty()) << "optimal row must not be empty";
    }
  }
}

TEST_F(ExploreViewTest, HeaderOptimalRowsHaveAtLeastOneRowPerEdit) {
  // At least totalEdits rows when totalEdits > 0 (one per edit, plus any
  // surrounding nav rows).
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto view = makeView(initial, {0, 0}, goal, {0, 10});

  ASSERT_GT(view.totalEdits(), 0);
  auto rows = view.headerRows();
  ASSERT_FALSE(rows.optimal.empty());
  for (const auto& col : rows.optimal) {
    EXPECT_GE(static_cast<int>(col.size()), view.totalEdits())
        << "expected at least one row per planned edit";
  }
}

} // namespace
