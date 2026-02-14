// tests/Exploration/ExplorationCollector.cpp
//
// Standalone binary that collects explored A* states from representative
// benchmark cases and outputs JSON for the dashboard visualization.
//
// Usage:
//   VIMFICIENCY_SEED_MODE=fixed ./build/tests/vimficiency_explore
//
// Outputs: motion_explore.json, edit_explore.json, composition_explore.json

#include <algorithm>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Boundary/EditBoundary.h"
#include "Boundary/MotionBoundary.h"
#include "Optimizer/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizer.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Optimizer/MotionOptimizer/MotionOptimizerParams.h"
#include "Optimizer/SearchStats.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

using namespace std;

static Config config = Config::uniform();

// =============================================================================
// Minimal JSON writer
// =============================================================================

static string jsonEscape(string_view s) {
  string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}

static string truncateSeq(const string& seq, size_t maxLen = 20) {
  if (seq.size() <= maxLen) return seq;
  return seq.substr(0, maxLen) + "...";
}

static void writeExplorationJson(const string& filename,
                                  const vector<pair<string, SearchStats>>& cases) {
  ofstream out(filename);
  out << "{\n  \"cases\": [\n";
  for (size_t i = 0; i < cases.size(); i++) {
    const auto& [name, stats] = cases[i];
    out << "    {\n";
    out << "      \"name\": \"" << jsonEscape(name) << "\",\n";
    out << "      \"nodesExplored\": " << stats.nodesExplored << ",\n";
    out << "      \"states\": [";
    for (size_t j = 0; j < stats.exploredStates.size(); j++) {
      const auto& s = stats.exploredStates[j];
      if (j > 0) out << ",";
      out << "\n        {\"effort\": " << s.effort
          << ", \"seq\": \"" << jsonEscape(truncateSeq(s.sequence)) << "\"}";
    }
    out << "\n      ]\n";
    out << "    }";
    if (i + 1 < cases.size()) out << ",";
    out << "\n";
  }
  out << "  ]\n}\n";
  cout << "Wrote " << filename << " (" << cases.size() << " cases)\n";
}

// =============================================================================
// Motion cases
// =============================================================================

static vector<pair<string, SearchStats>> collectMotionCases() {
  vector<pair<string, SearchStats>> cases;
  auto& seedMgr = SeedManager::instance();

  struct MotionCase {
    string name;
    int numLines;
    int avgLen;
  };

  vector<MotionCase> motionCases = {
    {"BufferSize/1", 1, 30},
    {"BufferSize/5", 5, 30},
    {"BufferSize/10", 10, 30},
    {"BufferSize/20", 20, 30},
    {"LineLength/10", 20, 10},
    {"LineLength/40", 20, 40},
    {"LineLength/80", 20, 80},
  };

  MotionOptimizerParams params;
  params.trackExploredStates = true;

  for (const auto& mc : motionCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines lines = randomCodeBuffer(mc.numLines, mc.avgLen);
    Position firstPos = randomFirstPos(lines);
    Position lastPos = randomLastPos(lines);
    Position boundaryEnd(lastPos.line, lastPos.col + 1);
    MotionBoundary boundary(lines, firstPos, boundaryEnd, true, true);

    MotionOptimizer opt(config);
    auto result = opt.optimize(lines, firstPos, lastPos, params, "", boundary);
    cases.emplace_back(mc.name, result.stats);
  }

  return cases;
}

// =============================================================================
// Edit cases
// =============================================================================

static vector<pair<string, SearchStats>> collectEditCases() {
  vector<pair<string, SearchStats>> cases;
  auto& seedMgr = SeedManager::instance();

  EditOptimizerParams params;
  params.trackExploredStates = true;

  // Pure deletion cases
  struct EditCase {
    string name;
    int numLines;
    int avgLen;
  };

  vector<EditCase> pureDeletionCases = {
    {"BufferSize/1", 1, 30},
    {"BufferSize/3", 3, 30},
    {"BufferSize/5", 5, 30},
    {"BufferSize/10", 10, 30},
    {"LineLength/10", 5, 10},
    {"LineLength/40", 5, 40},
    {"LineLength/60", 5, 60},
  };

  for (const auto& ec : pureDeletionCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines lines = randomCodeBuffer(ec.numLines, ec.avgLen);
    EditBoundary boundary(lines, Position(0, 0), lines.endPos());
    auto p = params;
    p.maxResults = max(10, lines.totalPositions() / 4);

    EditOptimizer opt(config);
    auto result = opt.optimizePureDeletion(lines, boundary, p);
    cases.emplace_back(ec.name, result.stats);
  }

  // Multi-line edit cases
  {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines buffer = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
    Lines goal = {"Florida"};
    EditBoundary boundary(buffer, Position(0, 23), Position(1, 19));
    Lines editRegion = buffer.getSpan(Position(0, 23), Position(1, 19));
    auto p = params;
    p.maxResults = max(10, editRegion.totalPositions() / 4);

    EditOptimizer opt(config);
    auto result = opt.optimizeEdit(editRegion, goal, boundary, p);
    cases.emplace_back("MultiLineEdit/2L->1w", result.stats);
  }

  return cases;
}

// =============================================================================
// Composition cases
// =============================================================================

static Lines generateBuffer(int numLines, int avgLineLen) {
  return randomCodeBuffer(numLines, avgLineLen);
}

static vector<pair<string, SearchStats>> collectCompositionCases() {
  vector<pair<string, SearchStats>> cases;
  auto& seedMgr = SeedManager::instance();

  CompositionOptimizerParams params;
  params.trackExploredStates = true;

  auto makeDefaultSetup = [](int numLines, int avgLen, int editCount) {
    Lines initial = generateBuffer(numLines, avgLen);
    Lines goal = initial;
    for (int e = 0; e < editCount; e++) {
      int line = editCount <= 1 ? numLines / 2
                                : e * (numLines - 1) / max(1, editCount - 1);
      int len = max(1, static_cast<int>(initial[line].size()));
      goal[line] = randomWord(len);
      if (goal[line] == initial[line]) goal[line] = "changed";
    }
    return make_pair(initial, goal);
  };

  struct CompCase {
    string name;
    int numLines;
    int avgLen;
    int editCount;
  };

  vector<CompCase> compCases = {
    {"EditCount/1", 15, 20, 1},
    {"EditCount/2", 15, 20, 2},
    {"EditCount/5", 15, 20, 5},
    {"EditCount/8", 15, 20, 8},
    {"BufferSize/5", 5, 20, 5},
    {"BufferSize/10", 10, 20, 5},
    {"BufferSize/20", 20, 20, 5},
  };

  for (const auto& cc : compCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    auto [initial, goal] = makeDefaultSetup(cc.numLines, cc.avgLen, cc.editCount);

    CompositionOptimizer opt(config);
    auto result = opt.optimize(initial, {0, 0}, goal, {0, 0}, params);
    cases.emplace_back(cc.name, result.stats);
  }

  return cases;
}

// =============================================================================
// Main
// =============================================================================

int main() {
  auto& seedMgr = SeedManager::instance();
  cout << "Exploration collector\n";
  cout << "Seed mode: " << (seedMgr.isRandom() ? "random" : "fixed") << "\n\n";

  auto motionCases = collectMotionCases();
  writeExplorationJson("motion_explore.json", motionCases);

  auto editCases = collectEditCases();
  writeExplorationJson("edit_explore.json", editCases);

  auto compositionCases = collectCompositionCases();
  writeExplorationJson("composition_explore.json", compositionCases);

  cout << "\nDone.\n";
  return 0;
}
