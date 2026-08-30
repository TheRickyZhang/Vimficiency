// tests/Unit/NavOptimizer/SearchControlTest.cpp
//
// NavOptimizer polls SearchControl per pop so async workers (analyze's
// nav-only branch, composition's nested motion searches) can bail early.
// Verifies: a non-triggering control is identical to no control, and a
// pre-tripped control stops before the first pop.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="NavSearchControl*"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/SearchControl.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

using namespace std;

namespace {

vector<string> sequenceStrings(const LandingNavResult& res) {
  vector<string> out;
  for (const auto& r : res.getResults()) out.push_back(r.getSequence().str());
  return out;
}

}  // namespace

class NavSearchControlTest : public ::testing::Test {
protected:
  Config config = Config::uniform();
  NavOptimizer opt{config};

  Lines lines = {"foo bar baz", "qux quux corge", "grault garply"};
  CursorPos initialPos{0, 0};
  CursorPos goalPos{2, 7};
};

TEST_F(NavSearchControlTest, NonTriggeringControlMatchesFullSearch) {
  LandingNavResult full = opt.optimize(lines, initialPos, goalPos);
  SearchControl control;  // no deadline, not cancelled: never stops.
  LandingNavResult guarded = opt.optimize(
      lines, initialPos, goalPos, NavOptimizerParams{}, "", NavBoundary(),
      NavContext(), &control);

  EXPECT_EQ(sequenceStrings(full), sequenceStrings(guarded));
  EXPECT_EQ(full.getStats().totalPops(), guarded.getStats().totalPops());
  EXPECT_GT(full.getStats().totalPops(), 0)
      << "fixture must exercise the loop for the contrast to be meaningful";
}

TEST_F(NavSearchControlTest, CancelledControlStopsBeforeFirstPop) {
  SearchControl control;
  control.cancelled.store(true);
  LandingNavResult res = opt.optimize(
      lines, initialPos, goalPos, NavOptimizerParams{}, "", NavBoundary(),
      NavContext(), &control);

  EXPECT_EQ(res.getStats().totalPops(), 0);
  EXPECT_TRUE(res.getResults().empty());
}

TEST_F(NavSearchControlTest, PastDeadlineStopsBeforeFirstPop) {
  SearchControl control;
  control.hasDeadline = true;
  control.deadline = chrono::steady_clock::now() - chrono::seconds(1);
  LandingNavResult res = opt.optimize(
      lines, initialPos, goalPos, NavOptimizerParams{}, "", NavBoundary(),
      NavContext(), &control);

  EXPECT_EQ(res.getStats().totalPops(), 0);
}
