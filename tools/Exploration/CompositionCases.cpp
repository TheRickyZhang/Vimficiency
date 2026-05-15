#include "Exploration/ExplorationCollector.h"

#include <algorithm>
#include <map>

#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

using namespace std;

static Lines generateBuffer(int numLines, int avgLineLen) {
  return randomCodeBuffer(numLines, avgLineLen);
}

vector<CompositionExploreCase> collectCompositionCases() {
  vector<CompositionExploreCase> cases;
  auto& seedMgr = SeedManager::instance();

  CompositionOptimizerParams params;
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

    vector<FoundResult> found;
    for (const auto& r : result.getResults()) {
      if (!r.getSequence().empty()) {
        found.push_back({r.getSequence().str(), r.getCost()});
      }
    }

    ContextData ctx;
    for (const auto& l : initial) ctx.initialLines.push_back(l);
    for (const auto& l : goal) ctx.goalLines.push_back(l);
    ctx.initialCursorLine = 0;
    ctx.initialCursorCol = 0;
    ctx.goalCursorLine = 0;
    ctx.goalCursorCol = 0;

    // Extract per-diff edit exploration data.
    vector<PerDiffEditExploration> editDetails;
    for (int editIndex = 0; editIndex < result.totalEdits(); editIndex++) {
      const auto plannedEdit = result.plannedEditAt(editIndex);
      const auto& transformResult = plannedEdit.transformResult;
      PerDiffEditExploration detail;
      detail.states = transformResult.getStats().exploredStates();

      // Collect unique best results from all starting positions
      map<string, double> bestBySeq;
      for (const auto& bucket : transformResult.getResults()) {
        for (const auto& r : bucket) {
          auto it = bestBySeq.find(r.getSequence().str());
          if (it == bestBySeq.end() || r.getCost() < it->second) {
            bestBySeq[r.getSequence().str()] = r.getCost();
          }
        }
      }
      for (const auto& [seq, effort] : bestBySeq) {
        detail.results.push_back({seq, effort});
      }
      sort(detail.results.begin(), detail.results.end(),
           [](const auto& a, const auto& b) { return a.effort < b.effort; });
      if (detail.results.size() > 10) detail.results.resize(10);

      editDetails.push_back(std::move(detail));
    }

    cases.push_back({cc.name, result.getStats().nodesExplored(),
                     std::move(found),
                     std::move(result.getExploredStates()),
                     std::move(ctx),
                     std::move(result.getDiffs()),
                     std::move(editDetails)});
  }

  return cases;
}
