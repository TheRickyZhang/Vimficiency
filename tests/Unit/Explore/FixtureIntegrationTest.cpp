// tests/Unit/Explore/FixtureIntegrationTest.cpp
//
// Integration tests for Explore::View driven by canonical saved-session
// JSON fixtures. Each fixture mirrors the real on-disk schema written by
// the Lua layer, so these tests exercise the same typed inputs that a live
// plugin session hands to the optimizer — no hand-rolled Lines/cursors.

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Explore/Explore.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

#include "Unit/Explore/FixtureLoader.h"

using namespace std;

namespace {

Explore::View viewFromFixture(const ExploreFixtures::Fixture& f,
                              NavContext navContext, Config config) {
  const int lastLine = max(0, static_cast<int>(f.lines.size()) - 1);
  const int lastLineSize = f.lines.empty()
      ? 0
      : static_cast<int>(f.lines[lastLine].size()) + 1;
  NavBoundary boundary(f.lines,
                          CursorPos(0, 0),
                          CursorPos(lastLine, lastLineSize),
                          f.hasLinesAbove,
                          f.hasLinesBelow);
  return Explore::View(f.lines, f.startPos, f.goalLines, f.endPos,
                       std::move(boundary), navContext, config,
                       /*userSequence=*/"");
}

class ExploreFixtureTest : public ::testing::Test {
 protected:
  Config config = Config::uniform();
  NavContext navContext{24, 12};
};

// Baseline: canonical fixture loads, view enters Navigate/Transform, and the
// frontier produces at least one distinct recommendation.
TEST_F(ExploreFixtureTest, RenameFixtureStartsInActivePhase) {
  auto f = ExploreFixtures::loadFixture("rename_int_n_to_m");
  auto view = viewFromFixture(f, navContext, config);

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()) ||
              std::holds_alternative<Explore::Transform>(view.phase()));
  EXPECT_GT(view.totalEdits(), 0);
  EXPECT_EQ(view.state().cursor, f.startPos);

  auto recs = view.recommendations(5);
  EXPECT_FALSE(recs.empty());

  set<string> texts;
  for (const auto& rec : recs) texts.insert(rec.token);
  EXPECT_EQ(texts.size(), recs.size()) << "recommendations should be distinct";
}

// Stepping test: after moving the cursor along the top motion rec, the
// frontier should continue to provide candidates until the plan completes.
TEST_F(ExploreFixtureTest, InsertFixtureDrivesForwardUntilCompletion) {
  auto f = ExploreFixtures::loadFixture("insert_plus_one");
  auto view = viewFromFixture(f, navContext, config);

  ASSERT_TRUE(std::holds_alternative<Explore::Navigate>(view.phase()) ||
              std::holds_alternative<Explore::Transform>(view.phase()));

  int steps = 0;
  const int maxSteps = 60;  // per-token walk; bounded generously above
                            // the Manhattan distance between start and target
  while ((std::holds_alternative<Explore::Navigate>(view.phase()) ||
          std::holds_alternative<Explore::Transform>(view.phase())) &&
         steps < maxSteps) {
    auto recs = view.recommendations(5);
    if (recs.empty()) {
      ADD_FAILURE()
          << "empty frontier at step " << steps
          << " cursor=(" << view.state().cursor.line << ","
          << view.state().cursor.col << ")"
          << " editIndex="
          << Explore::phaseIndex(view.phase());
      break;
    }

    // Navigate phase emits motions; Transform phase emits edits. This test
    // only walks the motion frontier, so break out once we hit Transform.
    if (!std::holds_alternative<Explore::Navigate>(view.phase())) break;
    ASSERT_TRUE(view.applyMovement(recs.front().token).has_value());
    steps++;
  }

  EXPECT_LT(steps, maxSteps);
}

}  // namespace
