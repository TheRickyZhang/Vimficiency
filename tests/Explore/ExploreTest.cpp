// tests/Explore/ExploreTest.cpp
//
// Tests for Explore::View: phase machine + recommendations + applyMovement
// + strict-revert buffer-state flow. Invalid phase is not a reachable state
// — programming-invariant failures assert, external teardown destroys the
// view — so there's no corresponding test here.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <string_view>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/TransformOptimizer/TransformFrontier.h"
#include "Optimizer/TransformOptimizer/TransformSequenceDecomposition.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/Result.h"
#include "Explore/Explore.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

class ExploreViewTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};

  Explore::View makeView(Lines initial, CursorPos initialPos, Lines goal,
                         CursorPos goalPos) {
    NavBoundary boundary(
        initial, CursorPos(0, 0),
        CursorPos(static_cast<int>(initial.size()) - 1,
                  static_cast<int>(initial.back().size()) + 1),
        /*hasLinesAbove=*/false,
        /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }

  Explore::View makeViewWithBoundary(Lines initial, CursorPos initialPos, Lines goal,
                                     CursorPos goalPos, CursorPos boundaryBegin,
                                     CursorPos boundaryEnd) {
    NavBoundary boundary(initial, boundaryBegin, boundaryEnd,
                         /*hasLinesAbove=*/false,
                         /*hasLinesBelow=*/false);
    return Explore::View(std::move(initial), initialPos, std::move(goal),
                         goalPos, std::move(boundary), navContext, config);
  }
};

TEST_F(ExploreViewTest, CompletedWhenInitialEqualsGoal) {
  Lines lines{Line("hello world")};
  auto view = makeView(lines, {0, 0}, lines, {0, 0});

  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.totalEdits(), 0);
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, PureMotionGoalStartsInNavigate) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  // Pure-motion: Navigate(0) with totalEdits == 0. The "no planned edit"
  // signal is now `totalEdits == 0`, not a missing optional index.
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), 0);
  EXPECT_EQ(view.totalEdits(), 0);

  auto range = view.currentTargetRange();
  EXPECT_EQ(range.first, CursorPos(0, 4));
  EXPECT_EQ(range.second, CursorPos(0, 4));

  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty());
  EXPECT_TRUE(any_of(recs.begin(), recs.end(), [](const Suggestion& rec) {
    return rec.landingPos.line == 0 && rec.landingPos.col == 4;
  }));
}

TEST_F(ExploreViewTest, PureMotionGoalCompletesWhenCursorReachesGoal) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().seq, "w");
  EXPECT_TRUE(view.recommendations(5).empty());
}

TEST_F(ExploreViewTest, CompletionIsDerivedAndNotSticky) {
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(view.isCompleted());

  ASSERT_TRUE(view.applyMovement("b").has_value());
  EXPECT_FALSE(view.isCompleted());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), view.totalEdits());

  ASSERT_TRUE(view.applyMovement("w").has_value());
  EXPECT_TRUE(view.isCompleted());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
}

TEST_F(ExploreViewTest, ApproachesEditWhenLinesDiffer) {
  Lines initial{Line("foo bar baz")};
  Lines goal{Line("foo QUX baz")};
  auto view = makeView(initial, {0, 0}, goal, {0, 10});

  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), 0);
  EXPECT_GT(view.totalEdits(), 0);
  EXPECT_EQ(view.state().cursor.line, 0);
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_TRUE(view.state().seq.empty());
}

TEST_F(ExploreViewTest, RecommendationsHonorPerCellDedupOverride) {
  // End-to-end check that overrides reach the frontier: with default
  // dedup (cap 1), each landing cell carries one motion. Setting
  // `nav:maxResultsPerEndPos` to a large value lifts the cap so
  // multiple distinct motions to the same cell can surface.
  Lines initial{Line("one two three four five")};
  Lines goal{Line("one two three four FIVE")};
  auto view = makeView(initial, {0, 0}, goal, {0, 19});

  // Default — one motion per landing cell.
  auto deduped = view.recommendations(20);

  // Override — disable the cap.
  const auto unCapped = OptimizerParamOverrides::parse(
      "nav:maxResultsPerEndPos=2147483647");
  auto wide = view.recommendations(20, &unCapped);

  // The override-broadened set should reach at least as many
  // recommendations as the default set (more landing-cell coverage,
  // and/or multiple tokens per cell).
  ASSERT_FALSE(deduped.empty()) << "default dedup produced no recs";
  EXPECT_GE(wide.size(), deduped.size())
      << "override should not shrink the result set";
}

TEST_F(ExploreViewTest, RecommendationsAreDiverse) {
  Lines initial{Line("foo bar baz qux zed")};
  Lines goal{Line("foo bar baz qux ZED")};
  auto view = makeView(initial, {0, 0}, goal, {0, 18});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(5);
  ASSERT_FALSE(recs.empty()) << "expected at least one motion recommendation";

  // Distinct recommendation texts — grouping/dedup works.
  set<string> texts;
  for (const auto& rec : recs)
    texts.insert(rec.token);
  EXPECT_EQ(texts.size(), recs.size());

  // Navigate phase, so all recs are motions; each must change the cursor.
  for (const auto& rec : recs) {
    const bool moved = rec.landingPos.line != 0 || rec.landingPos.col != 0;
    EXPECT_TRUE(moved) << "motion '" << rec.token << "' did not change cursor";
  }
}

TEST_F(ExploreViewTest, ApplyMotionAdvancesCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  auto outcome = view.applyMovement("w");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  EXPECT_EQ(view.state().cursor.col, 4);
  EXPECT_EQ(view.state().seq, "w");
  EXPECT_GT(view.state().cost, 0.0);
  EXPECT_TRUE(view.canUndo());
}

TEST_F(ExploreViewTest, RecommendationCostDiffIncludesAcceptedSequence) {
  config = Config::qwerty();
  config.weights.same_key_weight = 1.0;
  Lines lines{Line("foo bar baz")};
  auto view = makeView(lines, {0, 0}, lines, {0, 8});

  ASSERT_TRUE(view.applyMovement("w").has_value());

  auto recs = view.recommendations(5);
  auto rec = find_if(recs.begin(), recs.end(), [](const Suggestion& candidate) {
    return string_view(candidate.token) == "w";
  });
  ASSERT_NE(rec, recs.end());

  string combined = view.state().seq + string(rec->token);
  const double expected = getEffort(combined, config) - view.state().cost;
  const double standalone = getEffort(string(rec->token), config);

  EXPECT_NEAR(rec->costDiff, expected, 1e-9);
  EXPECT_GT(abs(rec->costDiff - standalone), 1e-9);
}

TEST_F(ExploreViewTest, ApplyMotionRejectsMalformedInput) {
  Lines initial{Line("abcd")};
  Lines goal{Line("abCd")};
  auto view = makeView(initial, {0, 0}, goal, {0, 3});

  auto outcome = view.applyMovement("<"); // incomplete special key
  ASSERT_FALSE(outcome.has_value());
  EXPECT_FALSE(outcome.error().reason.empty());
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, ApplyMotionRejectsBoundaryEscape) {
  Lines lines{Line("prefix body suffix")};
  auto view = makeViewWithBoundary(lines, {0, 7}, lines, {0, 10},
                                   CursorPos(0, 7), CursorPos(0, 11));

  auto outcome = view.applyMovement("$");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "motion landed outside the allowed boundary");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 7));
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, AcceptCursorMoveRejectsBoundaryEscape) {
  Lines lines{Line("prefix body suffix")};
  auto view = makeViewWithBoundary(lines, {0, 7}, lines, {0, 10},
                                   CursorPos(0, 7), CursorPos(0, 11));

  auto outcome = view.acceptCursorMove(CursorPos(0, 16), "");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "motion landed outside the allowed boundary");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 7));
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_FALSE(view.canUndo());
}

TEST_F(ExploreViewTest, AcceptCursorMoveTrustsObservedCursorOverRawReplay) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  auto outcome = view.acceptCursorMove(CursorPos(0, 4), "l");
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(view.state().cursor, CursorPos(0, 4));
  EXPECT_EQ(view.state().seq, "l");
  EXPECT_TRUE(view.canUndo());
}

TEST_F(ExploreViewTest, UndoRestoresPriorCursorAndSequence) {
  Lines initial{Line("foo bar baz qux")};
  Lines goal{Line("foo bar baz QUX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 14});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  const int cursorAfter = view.state().cursor.col;
  ASSERT_GT(cursorAfter, 0);

  auto undone = view.undo();
  ASSERT_TRUE(undone.has_value());
  EXPECT_EQ(view.state().cursor.col, 0);
  EXPECT_TRUE(view.state().seq.empty());
  EXPECT_TRUE(view.canRedo());

  auto redone = view.redo();
  ASSERT_TRUE(redone.has_value());
  EXPECT_EQ(view.state().cursor.col, cursorAfter);
  EXPECT_EQ(view.state().seq, "w");
}

TEST_F(ExploreViewTest, UndoFromCleanStateIsRejected) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.undo();
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason, "nothing to undo");
}

TEST_F(ExploreViewTest, BeginInsertTransitionsIntoInsert) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  auto outcome = view.beginInsert();
  ASSERT_TRUE(outcome.has_value());
  EXPECT_TRUE(std::holds_alternative<Explore::Insert>(view.phase()));
  EXPECT_EQ(Explore::phaseIndex(view.phase()), 0);
}

TEST_F(ExploreViewTest, InsertPhaseRecommendationCarriesTypedText) {
  // Replacement: cursor inside the diff range. The Insert recommendation's
  // token is the canonical text the user must type in insert mode to reach
  // the planned post-edit fencepost.
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  // Walk the cursor to the diff's first changed character.
  ASSERT_TRUE(view.applyMovement("w").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  ASSERT_TRUE(view.beginInsert().has_value());

  auto recs = view.recommendations(5);
  ASSERT_EQ(recs.size(), 1u);
  const Suggestion& item = recs[0];
  EXPECT_FALSE(string_view(item.token).empty());
  EXPECT_GT(item.costDiff, 0.0);
  // The token must contain the new char `m` somewhere — the diff may span
  // more than one position depending on what minimal-diff returns, but the
  // user must produce `m` for the result to match the goal.
  EXPECT_NE(string_view(item.token).find('m'), string_view::npos);
}

TEST_F(ExploreViewTest, InsertPhaseRecommendationForPureInsertion) {
  // Pure insertion: append `X` at end of line via `a` or `A`. The typed
  // text is `X`.
  Lines initial{Line("ab")};
  Lines goal{Line("abX")};
  auto view = makeView(initial, {0, 0}, goal, {0, 2});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(view.beginInsert().has_value());

  auto recs = view.recommendations(5);
  ASSERT_EQ(recs.size(), 1u);
  const Suggestion& item = recs[0];
  EXPECT_EQ(string_view(item.token), "X");
}

TEST_F(ExploreViewTest, InsertPhaseRecommendationEmptyForPureDeletion) {
  // Pure deletion: no insert-mode follow-up. Even if a caller manages to
  // park us in Insert, recommendInsert returns no items.
  Lines initial{Line("abcd")};
  Lines goal{Line("ad")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  // For a pure-deletion edit, beginInsert is normally not invoked by Lua —
  // but if it were, the rec list would be empty (no typed text needed).
  // Note: depending on how the diff is decomposed this scenario may not
  // park the view in a pure-deletion-only Insert; smoke-test that no error
  // is raised regardless.
  if (view.beginInsert().has_value() &&
      std::holds_alternative<Explore::Insert>(view.phase())) {
    auto recs = view.recommendations(5);
    // Either empty (pure deletion) or has one item (replacement masked as
    // deletion); both are acceptable outcomes — no crash, no garbage.
    EXPECT_LE(recs.size(), 1u);
  }
}

TEST_F(ExploreViewTest, NavigatePhaseSurfacesACompositionForEolInsertion) {
  // For an EOL pure insertion the optimizer telescopes Navigate+Transform
  // by emitting `A<text>` from any column on the line. The composition
  // frontier should surface this during Navigate phase so the user sees
  // the shortcut alongside pure motions toward the insertion point.
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  auto recs = view.recommendations(20);
  ASSERT_FALSE(recs.empty());

  // Composition motions include trailing <Esc> for the typed-payload exit
  // (mirrors what the optimizer emits in its flat sequences).
  const bool hasA = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "A!<Esc>"; });
  const bool hasMotionA = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "$a!<Esc>"; });
  EXPECT_TRUE(hasA)
      << "Navigate phase must surface `A!<Esc>` composition motion (zero-prefix)";
  EXPECT_TRUE(hasMotionA)
      << "Navigate phase must surface `$a!<Esc>` composition motion (motion-prefixed)";
}

TEST_F(ExploreViewTest, NavigateCompositionHonorsCompositionCountPrefixOverrides) {
  Lines initial;
  for (int i = 0; i < 20; i++) initial.push_back(Line("line " + to_string(i)));
  Lines goal = initial;
  goal.insert(goal.begin() + 5, Line("X"));
  auto view = makeView(initial, {0, 0}, goal, {5, 0});

  auto hasCountedLineInsert = [](const vector<Suggestion>& recs) {
    return any_of(recs.begin(), recs.end(), [](const Suggestion& s) {
      string_view token(s.token);
      return token.rfind("4j", 0) == 0 &&
             token.find("oX<Esc>") != string_view::npos;
    });
  };
  auto tokens = [](const vector<Suggestion>& recs) {
    string out;
    for (const auto& rec : recs) {
      if (!out.empty()) out += ", ";
      out += rec.token;
    }
    return out;
  };

  auto defaultRecs = view.recommendations(100);
  EXPECT_TRUE(hasCountedLineInsert(defaultRecs)) << tokens(defaultRecs);

  const auto disableCompositionCounts = OptimizerParamOverrides::parse(
      "composition:maxPrefixCount=0");
  auto withoutCounts = view.recommendations(
      100, &disableCompositionCounts, SuggestionSortMode::Effort);
  EXPECT_FALSE(hasCountedLineInsert(withoutCounts))
      << "composition frontier must pass composition count-prefix settings into nav: "
      << tokens(withoutCounts);
}

TEST_F(ExploreViewTest, CompositionRecommendationCostIncludesFullShortcut) {
  Lines initial{Line("hello")};
  Lines goal{Line("hello!")};
  auto view = makeView(initial, {0, 0}, goal, {0, 5});

  auto recs = view.recommendations(20);
  auto rec = find_if(recs.begin(), recs.end(), [](const Suggestion& s) {
    return string_view(s.token) == "$a!<Esc>";
  });
  ASSERT_NE(rec, recs.end());

  EXPECT_NEAR(rec->costDiff, getEffort("$a!<Esc>", config), 1e-9);
  EXPECT_GT(rec->costDiff, getEffort("$", config));
}

TEST_F(ExploreViewTest, TransformDeletionHonorsTransformCountPrefixOverrides) {
  Lines initial{Line("a"), Line("b"), Line("c"), Line("d"), Line("e")};
  Lines goal{Line("e")};
  auto view = makeView(initial, {0, 0}, goal, {0, 0});

  auto hasCountedDelete = [](const vector<Suggestion>& recs) {
    return any_of(recs.begin(), recs.end(), [](const Suggestion& s) {
      return string_view(s.token) == "4dd";
    });
  };
  auto tokens = [](const vector<Suggestion>& recs) {
    string out;
    for (const auto& rec : recs) {
      if (!out.empty()) out += ", ";
      out += rec.token;
    }
    return out;
  };

  auto defaultRecs = view.recommendations(100);
  EXPECT_TRUE(hasCountedDelete(defaultRecs)) << tokens(defaultRecs);

  const auto disableTransformCounts = OptimizerParamOverrides::parse(
      "transform:maxPrefixCount=0");
  auto withoutCounts = view.recommendations(
      100, &disableTransformCounts, SuggestionSortMode::Effort);
  EXPECT_FALSE(hasCountedDelete(withoutCounts))
      << "transform frontier must pass transform count-prefix settings: "
      << tokens(withoutCounts);
}

TEST_F(ExploreViewTest, TransformPhaseSurfacesReplaceCharForSingleCharDiff) {
  // TransformPostExplorer owns the finalize-time replacement emission; the
  // depth-1 frontier must surface it too.
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  ASSERT_TRUE(view.applyMovement("l").has_value());
  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));

  auto recs = view.recommendations(20);
  ASSERT_FALSE(recs.empty());
  const bool hasReplaceChar = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) { return string_view(s.token) == "rB"; });
  const bool hasDeleteFirst = any_of(recs.begin(), recs.end(),
      [](const Suggestion& s) {
        return string_view(s.token) == "x";
      });
  EXPECT_TRUE(hasReplaceChar)
      << "depth-1 transform must emit `rB` for single-char same-length replacement";
  EXPECT_TRUE(hasDeleteFirst)
      << "replacement transform must expose deletion-first prefixes like `x`";
}

TEST_F(ExploreViewTest, RecommendationSortModesUseDifferentMetrics) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 1}, goal, {0, 1});

  ASSERT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
  const auto overrides = OptimizerParamOverrides::parse(
      "transform:distanceWeight=2.0");

  auto indexOf = [](const vector<Suggestion>& recs, string_view token) {
    for (size_t i = 0; i < recs.size(); ++i) {
      if (string_view(recs[i].token) == token) return static_cast<int>(i);
    }
    return -1;
  };

  auto effort = view.recommendations(20, &overrides, SuggestionSortMode::Effort);
  auto distance = view.recommendations(20, &overrides, SuggestionSortMode::Distance);
  auto score = view.recommendations(20, &overrides, SuggestionSortMode::Score);

  const int effortReplace = indexOf(effort, "rB");
  ASSERT_GE(effortReplace, 0);

  const auto incomplete = find_if(effort.begin(), effort.end(),
      [&](const Suggestion& s) {
        return s.distance > 0.0 && s.costDiff < effort[effortReplace].costDiff;
      });
  ASSERT_NE(incomplete, effort.end());
  const string incompleteToken = incomplete->token;

  const int effortIncomplete = indexOf(effort, incompleteToken);
  const int distanceIncomplete = indexOf(distance, incompleteToken);
  const int distanceReplace = indexOf(distance, "rB");
  const int scoreIncomplete = indexOf(score, incompleteToken);
  const int scoreReplace = indexOf(score, "rB");

  ASSERT_GE(effortIncomplete, 0);
  ASSERT_GE(distanceIncomplete, 0);
  ASSERT_GE(distanceReplace, 0);
  ASSERT_GE(scoreIncomplete, 0);
  ASSERT_GE(scoreReplace, 0);

  EXPECT_LT(effortIncomplete, effortReplace);
  EXPECT_LT(distanceReplace, distanceIncomplete);
  EXPECT_LT(scoreReplace, scoreIncomplete);
  EXPECT_GT(effort[effortIncomplete].distance, 0.0);
  EXPECT_EQ(effort[effortReplace].distance, 0.0);
}


TEST_F(ExploreViewTest, OutOfScopeEditRejectedWithoutStateChange) {
  Lines initial{Line("int n = 10;")};
  Lines goal{Line("int m = 10;")};
  auto view = makeView(initial, {0, 0}, goal, {0, 4});

  ASSERT_TRUE(view.applyMovement("w").has_value());
  // Cursor is now on `n`. An edit command not in transformResult.resultsAt gets
  // rejected without mutating state.
  const auto priorState = view.state();
  auto outcome = view.applyEdit("totally-not-a-real-edit");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(view.state(), priorState);
  EXPECT_TRUE(std::holds_alternative<Explore::Transform>(view.phase()));
}

TEST_F(ExploreViewTest, AcceptBufferStateRejectsInvalidCursor) {
  Lines initial{Line("abc")};
  Lines goal{Line("aBc")};
  auto view = makeView(initial, {0, 0}, goal, {0, 1});

  auto outcome = view.acceptBufferState(goal, CursorPos(0, 99), "rB");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_EQ(outcome.error().reason,
            "buffer state reported an invalid cursor position");
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
  EXPECT_EQ(view.state().lines, initial);
  EXPECT_TRUE(view.state().seq.empty());
}

TEST(ExtractStructuralToken, ReturnsFirstNonTypedTextToken) {
  EXPECT_EQ(extractStructuralToken("sm<Esc>"), "s");
  EXPECT_EQ(extractStructuralToken("clm<Esc>"), "cl");
  EXPECT_EQ(extractStructuralToken("clfoo<Esc>"), "cl");
  EXPECT_EQ(extractStructuralToken("Jj"), "J");
  EXPECT_EQ(extractStructuralToken("x"), "x");
  EXPECT_EQ(extractStructuralToken("rm"), "rm");
  EXPECT_EQ(extractStructuralToken(""), "");
}

TEST(TransformFrontier, PreservesDistinctResultsFromSameStart) {
  DiffState diff(CursorPos(0, 0), CursorPos(0, 1), "x", "foo", TransformBoundary{});
  // Test's intent: verify that multiple distinct command-shape tokens
  // reaching the same diff goal are preserved by the frontier.
  // TransformExplorer enumerates per-shape lanes inherently, so no
  // override is needed to surface multiple tokens.
  auto recs = rankTransformFrontier(
      TransformFrontierQuery{
          FrontierQuery{
              .lines = Lines{Line("x")},
              .cursor = {0, 0},
              .maxCount = 10,
          },
          diff,
      },
      Config::uniform());
  ASSERT_GE(recs.size(), 2u);
  // Distinct structural tokens — the test's intent: multiple strategies for
  // the same diff produce different command shapes, all preserved.
  EXPECT_NE(recs[0].token, recs[1].token);
}

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
  // Pure-motion goal (initial == goal, cursor differs) never reaches Transform.
  Lines lines{Line("foo bar")};
  auto view = makeView(lines, {0, 0}, lines, {0, 4});

  auto outcome = view.applyEdit("x");
  ASSERT_FALSE(outcome.has_value());
  EXPECT_NE(outcome.error().reason.find("transforming"), string::npos);
  EXPECT_EQ(view.state().lines, lines);
  EXPECT_EQ(view.state().cursor, CursorPos(0, 0));
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

TEST_F(ExploreViewTest, NavigateMotionRecommendationsLandOnEditStarts) {
  Lines initial{Line("one two three four five six seven")};
  Lines goal{Line("one two three four FIVE six seven")};
  auto view = makeView(initial, {0, 0}, goal, {0, 22});

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()));
  const auto overrides = OptimizerParamOverrides::parse(
      "nav:maxResultsPerEndPos=2147483647");
  auto recs = view.recommendations(10, &overrides, SuggestionSortMode::Score);
  ASSERT_FALSE(recs.empty());

  set<string> seen;
  int motionCount = 0;
  bool hasRecoveryMotion = false;
  for (const auto& rec : recs) {
    ASSERT_TRUE(seen.insert(string(rec.token)).second)
        << "recommendations should be distinct";

    auto parsed = parseSequence(rec.token);
    if (!parsed) continue;
    const bool isMotion = all_of(parsed->begin(), parsed->end(),
        [](const TaggedToken& token) {
          return token.kind == TokenKind::Movement;
        });
    if (!isMotion) continue;

    motionCount++;
    auto probe = makeView(initial, {0, 0}, goal, {0, 22});
    auto moved = probe.acceptCursorMove(rec.landingPos, rec.token);
    ASSERT_TRUE(moved.has_value()) << "motion rec rejected: " << rec.token;
    if (std::holds_alternative<Explore::Transform>(probe.phase())) {
      hasRecoveryMotion = true;
    }
    EXPECT_EQ(probe.state().cursor, rec.landingPos);
  }
  EXPECT_GT(motionCount, 0);
  EXPECT_TRUE(hasRecoveryMotion);
}

// ---------------------------------------------------------------------------
// Header rows / edit-span pushing
// ---------------------------------------------------------------------------

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
