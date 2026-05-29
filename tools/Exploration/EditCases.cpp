#include "Exploration/ExplorationCollector.h"

#include <algorithm>
#include <map>

#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

vector<ExploreCase> collectEditCases() {
  vector<ExploreCase> cases;

  // Deduplicate the per-start result buckets into unique sequences, keep the
  // 10 cheapest.
  auto collectEditResults = [](const TransformResult& result) {
    map<string, double> bestBySeq;
    for (const auto& bucket : result.getResults()) {
      for (const auto& r : bucket) {
        auto it = bestBySeq.find(r.getSequence().str());
        if (it == bestBySeq.end() || r.getCost() < it->second) {
          bestBySeq[r.getSequence().str()] = r.getCost();
        }
      }
    }
    vector<FoundResult> found;
    for (const auto& [seq, effort] : bestBySeq) found.push_back({seq, effort});
    sort(found.begin(), found.end(),
         [](const auto& a, const auto& b) { return a.effort < b.effort; });
    if (found.size() > 10) found.resize(10);
    return found;
  };

  for (const auto& spec : editCaseCatalog()) {
    // Seed 0 is the representative instance of the multi-seed benchmark case.
    EditSetup s = buildEditSetup(spec, 0);

    TransformOptimizer opt(config);
    TransformResult result =
        s.goalKind == EditGoalKind::PureDeletion
            ? opt.optimizePureDeletion(s.initialLines, s.boundary, s.params)
            : opt.optimizeTransform(s.initialLines, s.goalLines, s.boundary,
                                    s.params);

    ContextData ctx;
    for (const auto& l : s.initialLines) ctx.initialLines.push_back(l);
    ctx.initialCursorLine = 0;
    ctx.initialCursorCol = 0;
    if (s.goalKind == EditGoalKind::Transform) {
      for (const auto& l : s.goalLines) ctx.goalLines.push_back(l);
      ctx.goalCursorLine = 0;
      ctx.goalCursorCol = 0;
    }  // pure deletion leaves goalLines empty
    ctx.prefix = s.boundary.prefix();
    ctx.suffix = s.boundary.suffix();
    ctx.hasLinesAbove = s.boundary.hasLinesAbove();
    ctx.hasLinesBelow = s.boundary.hasLinesBelow();

    cases.push_back(
        {spec.name, result.getStats(), collectEditResults(result), std::move(ctx)});
  }

  return cases;
}
