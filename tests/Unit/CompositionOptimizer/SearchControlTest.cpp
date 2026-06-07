// tests/Unit/CompositionOptimizer/SearchControlTest.cpp
//
// SearchControl lets the async explore worker bail the composition A* search
// early (cancel or deadline). Verifies: a non-triggering control is identical
// to no control, and a pre-tripped control stops before the first pop without
// crashing.
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

TEST_F(CompositionSearchControlTest, CancelledControlStopsBeforeFirstPop) {
  SearchControl control;
  control.cancelled.store(true);
  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "",
      boundary, NavContext(), &control);

  EXPECT_EQ(res.getStats().totalPops(), 0)
      << "cancelled control must bail the A* loop before any pop";
}

TEST_F(CompositionSearchControlTest, PastDeadlineStopsBeforeFirstPop) {
  SearchControl control;
  control.hasDeadline = true;
  control.deadline = chrono::steady_clock::now() - chrono::seconds(1);
  CompositionResult res = opt.optimize(
      initial, initialPos, goal, goalPos, CompositionOptimizerParams{}, "",
      boundary, NavContext(), &control);

  EXPECT_EQ(res.getStats().totalPops(), 0)
      << "an already-passed deadline must bail before any pop";
}
