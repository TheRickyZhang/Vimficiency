#include "Unit/Explore/TestHelpers.h"

using namespace std;

namespace {

TEST_F(ExploreViewTest, AcceptInsertExitAdvancesPhaseOnMatchingBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  // Get the cursor to the edit target via the cheapest motion.
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  // Navigate phase: every rec is a motion. Apply the first.
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());

  // Simulate the Lua layer: beginInsert parks us in Insert; the post-insert
  // buffer then validates via acceptInsertExit.
  ASSERT_TRUE(view.beginInsert().has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));

  auto outcome = view.acceptInsertExit(goal, {0, 2}, "");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().lines, goal);
  // Single-edit plan → advancing past the last edit lands in Navigate(totalEdits),
  // the post-final-edit nav segment. Completion is then a derived predicate
  // that triggers once the cursor reaches goalPos (here goalPos=(0,1) but
  // the insert exit lands at (0,2), so isCompleted is false until a
  // subsequent motion lands on goalPos).
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), view.totalEdits());
  EXPECT_FALSE(view.isCompleted());
}

TEST_F(ExploreViewTest, AcceptInsertExitRejectsMismatchedBuffer) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
  ASSERT_TRUE(view.beginInsert().has_value());
  const auto priorState = view.state();

  Lines wrong{Line("aXc")};
  auto outcome = view.acceptInsertExit(wrong, {0, 2}, "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, AcceptInsertExitRejectsInvalidCursor) {
  // Mirror of AcceptBufferStateRejectsInvalidCursor: a buffer-state-bearing
  // Insert completion must validate the reported cursor, otherwise a bad
  // position would be committed and poison the next phase.
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
  ASSERT_TRUE(view.beginInsert().has_value());
  const auto priorState = view.state();

  auto outcome = view.acceptInsertExit(goal, CursorPos(0, 99), "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason,
            "buffer state reported an invalid cursor position");
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, AcceptSnapshotLetsCursorLeaveTransformRange) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  auto outcome = view.acceptSnapshot(initial, CursorPos(0, 0), "h", false);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_EQ(view.state().seq, "lh");
}

TEST_F(ExploreViewTest, MovementAwayFromEditStartReturnsToNavigate) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  auto movedAway = view.applyMovement("h");
  ASSERT_TRUE(movedAway.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  EXPECT_TRUE(any_of(recs.begin(), recs.end(), [](const Suggestion& rec) {
    return rec.landingPos == CursorPos(0, 1);
  }));
}

TEST_F(ExploreViewTest, AcceptSnapshotBeginsInsertFromStructuralDeletion) {
  Lines initial{Line("n")};
  Lines goal{Line("m")};
  auto view = makeView(initial, {0, 0}, goal, {0, 0});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  Lines insertEntry{Line("")};
  auto outcome = view.acceptSnapshot(insertEntry, CursorPos(0, 0), "ci", true);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_TRUE(view.state().seq.empty())
      << "insert structural keys are recorded with the completed insert edit";

  auto recs = view.recommendations(1);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_EQ(string_view(recs[0].token), "m");
}

TEST_F(ExploreViewTest, AcceptSnapshotKeepsNormalDeletionInTransform) {
  Lines initial{Line("n")};
  Lines goal{Line("m")};
  auto view = makeView(initial, {0, 0}, goal, {0, 0});

  Lines insertEntry{Line("")};
  auto deleted = view.acceptSnapshot(insertEntry, CursorPos(0, 0), "x", false);
  ASSERT_TRUE(deleted.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_EQ(view.state().lines, insertEntry);
  EXPECT_EQ(view.state().seq, "x");

  auto recs = view.recommendations(10);
  ASSERT_FALSE(recs.empty());
  EXPECT_TRUE(any_of(recs.begin(), recs.end(), [](const Suggestion& s) {
    return string_view(s.token) == "i" || string_view(s.token) == "I";
  }));

  ASSERT_TRUE(view.acceptSnapshot(insertEntry, CursorPos(0, 0), "i", true).has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));

  auto completed = view.acceptSnapshot(goal, CursorPos(0, 0), "im<Esc>", false);
  ASSERT_TRUE(completed.has_value());
  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().seq, "xim<Esc>");
  ASSERT_EQ(view.state().editSpans.size, 1);
  EXPECT_EQ(view.state().editSpans.spans[0].beginByte, 0u);
  EXPECT_EQ(view.state().editSpans.spans[0].endByte, view.state().seq.size());
}

TEST_F(ExploreViewTest, UndoSkipsDeletePrefixReplacementIntermediate) {
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_EQ(view.state().seq, "w");

  Lines insertEntry{Line("int  = 10;")};
  ASSERT_TRUE(view.acceptSnapshot(
      insertEntry, CursorPos(0, 4), "x", false).has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_TRUE(view.state().hasPartialEditSpan);
  EXPECT_EQ(view.state().seq, "wx");

  ASSERT_TRUE(view.acceptSnapshot(
      insertEntry, CursorPos(0, 4), "i", true).has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));

  ASSERT_TRUE(view.acceptSnapshot(
      goal, CursorPos(0, 4), "im<Esc>", false).has_value());
  ASSERT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().seq, "wxim<Esc>");
  ASSERT_EQ(view.state().editSpans.size, 1);
  EXPECT_EQ(view.state().editSpans.spans[0].beginByte, 1u);
  EXPECT_EQ(view.state().editSpans.spans[0].endByte, view.state().seq.size());

  ASSERT_TRUE(view.undo().has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_EQ(view.state().lines, initial);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().seq, "w");

  ASSERT_TRUE(view.undo().has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().lines, initial);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_TRUE(view.state().seq.empty());

  ASSERT_TRUE(view.redo().has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_EQ(view.state().seq, "w");

  ASSERT_TRUE(view.redo().has_value());
  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().seq, "wxim<Esc>");
}

TEST_F(ExploreViewTest, CancelInsertAfterDeletePrefixKeepsPartialTransform) {
  Lines initial{Line("n")};
  Lines goal{Line("m")};
  auto view = makeView(initial, {0, 0}, goal, {0, 0});

  Lines insertEntry{Line("")};
  ASSERT_TRUE(view.acceptSnapshot(
      insertEntry, CursorPos(0, 0), "x", false).has_value());
  ASSERT_TRUE(view.acceptSnapshot(
      insertEntry, CursorPos(0, 0), "i", true).has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));

  ASSERT_TRUE(view.cancelInsert().has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_EQ(view.state().lines, insertEntry);
  EXPECT_EQ(view.state().seq, "x");
  EXPECT_TRUE(view.state().hasPartialEditSpan);
  EXPECT_FALSE(view.canRedo());

  ASSERT_TRUE(view.undo().has_value());
  EXPECT_EQ(view.state().lines, initial);
  EXPECT_TRUE(view.state().seq.empty());
}

TEST_F(ExploreViewTest, AcceptSnapshotCompletesInsertExit) {
  Lines initial{Line("n")};
  Lines goal{Line("m")};
  auto view = makeView(initial, {0, 0}, goal, {0, 0});

  Lines insertEntry{Line("")};
  ASSERT_TRUE(view.acceptSnapshot(insertEntry, CursorPos(0, 0), "ci", true).has_value());

  auto outcome = view.acceptSnapshot(goal, CursorPos(0, 0), "cim<Esc>", false);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().lines, goal);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_EQ(view.state().seq, "cim<Esc>");
  EXPECT_TRUE(view.isCompleted());
}

TEST_F(ExploreViewTest, AcceptSnapshotAllowsInsertCursorPastEolForPureInsertion) {
  Lines initial{Line("ab")};
  Lines goal{Line("abX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 2});

  auto outcome = view.acceptSnapshot(initial, CursorPos(0, 2), "A", true);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state().cursor, CursorPos(0, 2));

  auto recs = view.recommendations(1);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_EQ(string_view(recs[0].token), "X");
}

TEST_F(ExploreViewTest, AcceptSnapshotAllowsInsertEntryThatCreatesLine) {
  Lines initial{Line("a")};
  Lines goal{Line("a"), Line("X")};
  auto view = makeView(initial, {0, 0}, goal, {1, 0});

  Lines insertEntry{Line("a"), Line("")};
  auto outcome = view.acceptSnapshot(insertEntry, CursorPos(1, 0), "o", true);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state().lines, insertEntry);

  auto recs = view.recommendations(1);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_EQ(string_view(recs[0].token), "X");
}

TEST_F(ExploreViewTest, AcceptSnapshotPureDeletionDoesNotEnterInsert) {
  Lines initial{Line("abcd")};
  Lines goal{Line("ad")};
  auto view = makeView(initial, {0, 1}, goal, {0, 1});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  auto outcome = view.acceptSnapshot(goal, CursorPos(0, 1), "d", false);
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), view.totalEdits());
  EXPECT_EQ(view.state().lines, goal);
  EXPECT_TRUE(view.isCompleted());
}

TEST_F(ExploreViewTest, AcceptSnapshotRejectsInsertOutsidePlannedEditRange) {
  Lines initial{Line("ab cde")};
  Lines goal{Line("ab de")};
  auto view = makeView(initial, {0, 0}, goal, {0, 3});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  ASSERT_TRUE(view.acceptSnapshot(goal, CursorPos(0, 3), "x", false).has_value());
  ASSERT_TRUE(view.isCompleted());

  auto outcome = view.acceptSnapshot(goal, CursorPos(0, 4), "a", true);
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason,
            "insert-mode entry outside planned edit range");
  EXPECT_TRUE(view.isCompleted());
}

// =============================================================================
// Action-contract rejections
// =============================================================================
// One row per (action × invalid-input-category). Adding a new action means
// adding the corresponding rows here so the contract listed in Explore.h
// is enforced by tests, not by author memory.

TEST_F(ExploreViewTest, AcceptCursorMoveRecordsUnparseableRawKeys) {
  Lines initial{Line("foo bar")};
  Lines goal{Line("foo BAR")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  auto outcome = view.acceptCursorMove(CursorPos(0, 4), "<");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().seq, "<");
}

TEST_F(ExploreViewTest, AcceptCursorMoveAcceptsLiveSpaceNotation) {
  Lines initial{Line("abc def")};
  Lines goal{Line("abc def")};
  auto view = makeView(initial, {0, 0}, goal, {0, 3});

  auto outcome = view.acceptCursorMove(CursorPos(0, 3), "f<Space>");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 3));
  EXPECT_EQ(view.state().seq, "f<Space>");
  EXPECT_DOUBLE_EQ(view.state().cost, getEffort("f<Space>", config));
}

TEST_F(ExploreViewTest, AcceptBufferStateRecordsUnparseableRawKeys) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.acceptBufferState(goal, CursorPos(0, 1), "<");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().lines, goal);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 1));
  EXPECT_EQ(view.state().seq, "<");
}

TEST_F(ExploreViewTest, AcceptInsertExitRecordsUnparseableRawKeys) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
  ASSERT_TRUE(view.beginInsert().has_value());

  auto outcome = view.acceptInsertExit(goal, CursorPos(0, 1), "<");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().lines, goal);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 1));
  EXPECT_NE(view.state().seq.find("<"), string::npos);
}

TEST_F(ExploreViewTest, ApplyEditRejectedForMotionOnlyGoals) {
  // Pure-motion goal (initial == goal, cursor differs) has no planned edit
  // slot, so applyEdit is gated out by requirePlannedEditTarget.
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.applyEdit("x");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("post-final-edit"), string::npos);
  EXPECT_EQ(view.state().lines, lines);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
}

TEST_F(ExploreViewTest, ApplyEditAcceptsCompositionFromNavigate) {
  // EOL pure-insertion: from any column on the target line the composition
  // recommendation is `A`. Under the unified apply abstraction it must be
  // applicable even when the phase is Navigate (i.e. cursor is not at the
  // ordinary Transform-start position).
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto outcome = view.applyEdit("A");
  ASSERT_TRUE(outcome.has_value()) << outcome.error().reason;
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(view.state().cursor, CursorPos(0, 5));
}

TEST_F(ExploreViewTest, ApplyEditRejectsFullCompositionInsertionBody) {
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  const auto priorState = view.state();
  auto outcome = view.applyEdit("A!<Esc>");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("planned edit scope"), string::npos);
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, ApplyEditRejectsFullCompositionTextObjectBody) {
  Lines initial{Line("foo (abc) bar")};
  Lines goal{Line("foo (X) bar")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  const auto priorState = view.state();
  auto outcome = view.applyEdit("ci(X<Esc>");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("planned edit scope"), string::npos);
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, ApplyEditAcceptsJoinPlanProgress) {
  // The transform splits into a `\n`->` ` join (edit 0) and an insert "!"
  // (edit 1). A single J fully performs the join, completing edit 0 and
  // advancing — so it is accepted and the span is not left partial.
  Lines initial{Line("a"), Line("b")};
  Lines goal{Line("a b!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 3});

  auto outcome = view.applyEdit("J");
  ASSERT_TRUE(outcome.has_value()) << outcome.error().reason;
  EXPECT_EQ(view.state().lines, Lines{Line("a b")});
  EXPECT_FALSE(view.state().hasPartialEditSpan);
}

TEST_F(ExploreViewTest, ApplyEditRejectsUnplannedJoin) {
  Lines initial{Line("abc"), Line("def")};
  Lines goal{Line("aBc"), Line("def")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  const auto priorState = view.state();
  auto outcome = view.applyEdit("J");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("planned edit scope"), string::npos);
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, ApplyEditRejectsBogusToken) {
  // Lookup-only validation: a token outside the planner-sanctioned scope at
  // the current cursor must reject without mutating state.
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  const auto priorState = view.state();
  auto outcome = view.applyEdit("totally-not-a-real-edit");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("planned edit scope"), string::npos);
  EXPECT_EQ(view.state(), priorState);
}

TEST_F(ExploreViewTest, AcceptBufferStateRejectedAtPostFinalEditNav) {
  // Pure-motion sessions live entirely in Navigate(totalEdits). There is no
  // edit to apply, so buffer-state changes are rejected.
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.acceptBufferState(lines, CursorPos(0, 4), "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("post-final-edit"), string::npos);
}

TEST_F(ExploreViewTest, BeginInsertRejectedAtPostFinalEditNav) {
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.beginInsert();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("post-final-edit"), string::npos);
}

TEST_F(ExploreViewTest, UndoRedoSkipsInsertPhaseAcrossEdit) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  ASSERT_TRUE(view.beginInsert().has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));

  ASSERT_TRUE(view.acceptInsertExit(goal, {0, 2}, "rB").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().lines, goal);

  ASSERT_TRUE(view.undo().has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_EQ(view.state().lines, initial);

  ASSERT_TRUE(view.redo().has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().lines, goal);
  EXPECT_FALSE(view.canRedo());
}

TEST_F(ExploreViewTest, CancelInsertRestoresPreviousPhaseWithoutRedo) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  ASSERT_TRUE(view.beginInsert().has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_FALSE(view.canRedo());

  auto outcome = view.cancelInsert();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  // Crucially: redo stack NOT polluted with the rejected/abandoned insert.
  EXPECT_FALSE(view.canRedo());
}

} // namespace
