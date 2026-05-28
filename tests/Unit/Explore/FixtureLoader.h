#pragma once

// Canonical saved-session JSON loader for Explore integration tests.
// Mirrors the on-disk schema written by
// lua/vimficiency/session.lua:save_results so that tests can replay
// actual program state end-to-end instead of hand-rolling Lines/cursors.

#include <string>
#include <string_view>
#include <vector>

#include "Types/CursorPos.h"
#include "Types/Lines.h"

namespace ExploreFixtures {

struct OptimalEntry {
  std::string seq;
  double cost = 0.0;
};

struct Fixture {
  Lines lines;
  Lines goalLines;
  CursorPos startPos{0, 0};
  CursorPos endPos{0, 0};
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
  int windowHeight = 40;
  int scrollAmount = 20;
  std::string userSeq;
  std::vector<OptimalEntry> optimalResults;
};

// Load a fixture JSON by bare name (no extension). The file is resolved
// relative to tests/Unit/Explore/fixtures/.
Fixture loadFixture(std::string_view name);

}  // namespace ExploreFixtures
