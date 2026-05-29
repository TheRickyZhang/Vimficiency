#include "Exploration/ExplorationCollector.h"

#include <algorithm>
#include <map>

#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

vector<CompositionExploreCase> collectCompositionCases() {
  vector<CompositionExploreCase> cases;

  for (const auto& spec : compositionCaseCatalog()) {
    // Seed 0 is the representative instance of the multi-seed benchmark case.
    CompositionSetup s = buildCompositionSetup(spec, 0);

    CompositionOptimizer opt(config);
    auto result = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, s.params);

    vector<FoundResult> found;
    for (const auto& r : result.getResults()) {
      if (!r.getSequence().empty()) {
        found.push_back({r.getSequence().str(), r.getCost()});
      }
    }

    ContextData ctx;
    for (const auto& l : s.initial) ctx.initialLines.push_back(l);
    for (const auto& l : s.goal) ctx.goalLines.push_back(l);
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

    cases.push_back({spec.name, result.getStats().nodesExplored(),
                     std::move(found),
                     std::move(result.getExploredStates()),
                     std::move(ctx),
                     std::move(result.getDiffs()),
                     std::move(editDetails)});
  }

  return cases;
}
