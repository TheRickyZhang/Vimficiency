#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/DiffState.h"
#include "Optimizer/SearchStats.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"

struct FoundResult {
  std::string sequence;
  double effort;
};

struct ContextData {
  std::vector<std::string> initialLines;
  std::vector<std::string> goalLines;
  int initialCursorLine = -1, initialCursorCol = -1;
  int goalCursorLine = -1, goalCursorCol = -1;
  // Range goal (motion-to-range cases): the [begin, end) span of acceptable
  // landing cells. All -1 for a point goal, in which case goalCursor* applies.
  int goalRangeBeginLine = -1, goalRangeBeginCol = -1;
  int goalRangeEndLine = -1, goalRangeEndCol = -1;
  std::string prefix;
  std::string suffix;
  bool hasLinesAbove = false;
  bool hasLinesBelow = false;
};

struct ExploreCase {
  std::string name;
  BaseSearchStats stats;
  std::vector<FoundResult> results;
  ContextData context;
};

struct PerDiffEditExploration {
  std::vector<ExploredState> states;
  std::vector<FoundResult> results;
};

struct CompositionExploreCase {
  std::string name;
  int nodesExplored;
  std::vector<FoundResult> results;
  std::vector<CompositionExploredState> states;
  ContextData context;
  std::vector<DiffState> diffs;
  std::vector<PerDiffEditExploration> editDetails;
};

extern Config config;

std::string jsonEscape(std::string_view s);
std::vector<std::string> tokenizeSequence(const std::string& seq);
void writeContextJson(std::ofstream& out, const ContextData& ctx);

void writeExplorationJson(const std::string& filename,
                          const std::vector<ExploreCase>& cases);
void writeCompositionExplorationJson(
    const std::string& filename,
    const std::vector<CompositionExploreCase>& cases);

std::vector<ExploreCase> collectMotionCases();
std::vector<ExploreCase> collectEditCases();
std::vector<CompositionExploreCase> collectCompositionCases();
