// tests/Unit/CompositionOptimizer/SearchControlTest.cpp
//
// SearchControl lets the async explore worker bail the composition search
// early. Verifies: a non-triggering control is identical to no control; a
// pre-tripped cancel aborts during setup (empty plan, no pops); a passed
// deadline lets setup finish and stops the A* loop before its first pop.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="CompositionSearchControl*"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/SearchControl.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

vector<string> sequenceStrings(const CompositionResult& res) {
  vector<string> out;
  for (const auto& r : res.getResults()) out.push_back(r.getSequence().str());
  return out;
}

}  // namespace

class CompositionSearchControlTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  CompositionOptimizer opt{config};

  // Multi-edit case so the full search does real work to contrast against.
  Lines initial = {"foo bar baz qux"};
  Lines goal = {"foo BAR baz QUX"};
  CursorPos initialPos{0, 0};
  CursorPos goalPos{0, 0};
  NavBoundary boundary{initial, initialPos, initial.endPos()};
};

TEST_F(CompositionSearchControlTest, NonTriggeringControlMatchesFullSearch) {
  CompositionResult full = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "",
      boundary);
  SearchControl control;  // no deadline, not cancelled: never stops.
  CompositionResult guarded = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "",
      boundary, NavContext(), &control);

  EXPECT_EQ(sequenceStrings(full), sequenceStrings(guarded));
  EXPECT_EQ(full.getStats().totalPops(), guarded.getStats().totalPops());
  EXPECT_GT(full.getStats().totalPops(), 0)
      << "fixture must exercise the loop for the contrast to be meaningful";
}

TEST_F(CompositionSearchControlTest, CancelledControlAbortsSetup) {
  SearchControl control;
  control.cancelled.store(true);
  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "",
      boundary, NavContext(), &control);

  EXPECT_EQ(res.totalEdits(), 0)
      << "cancel must abort before per-edit precompute completes";
  EXPECT_TRUE(res.getResults().empty());
  EXPECT_EQ(res.getStats().totalPops(), 0);
}

TEST_F(CompositionSearchControlTest, PastDeadlineFinishesSetupThenStopsBeforeFirstPop) {
  SearchControl control;
  control.hasDeadline = true;
  control.deadline = chrono::steady_clock::now() - chrono::seconds(1);
  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "",
      boundary, NavContext(), &control);

  EXPECT_GT(res.totalEdits(), 0)
      << "a deadline must not abort setup; the plan is still produced";
  EXPECT_EQ(res.getStats().totalPops(), 0)
      << "an already-passed deadline must bail before any pop";
  EXPECT_FALSE(res.getResults().empty())
      << "best-so-far at zero pops is the whole-buffer rewrite fallback";
}
