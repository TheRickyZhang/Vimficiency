#include "Exploration/ExplorationCollector.h"

#include <algorithm>
#include <map>

#include "Boundary/TransformBoundary.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

using namespace std;

static TransformResult pureDeletionResult(
    TransformOptimizer& opt,
    const Lines& initialLines,
    TransformBoundary boundary,
    TransformOptimizerParams params = {}) {
  return opt.optimizePureDeletion(initialLines, boundary, params);
}

vector<ExploreCase> collectEditCases() {
  vector<ExploreCase> cases;
  auto& seedMgr = SeedManager::instance();

  TransformOptimizerParams params;
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

  auto collectEditResults = [](const TransformResult& result) {
    vector<FoundResult> found;
    // Deduplicate: TransformResult has results per starting position,
    // collect unique valid sequences sorted by effort
    map<string, double> bestBySeq;
    for (const auto& bucket : result.getResults()) {
      for (const auto& r : bucket) {
        auto it = bestBySeq.find(r.getSequence().str());
        if (it == bestBySeq.end() || r.getCost() < it->second) {
          bestBySeq[r.getSequence().str()] = r.getCost();
        }
      }
    }
    for (const auto& [seq, effort] : bestBySeq) {
      found.push_back({seq, effort});
    }
    sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
      return a.effort < b.effort;
    });
    // Keep top 10
    if (found.size() > 10) found.resize(10);
    return found;
  };

  for (const auto& ec : pureDeletionCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines lines = randomCodeBuffer(ec.numLines, ec.avgLen);
    TransformBoundary boundary(lines, CursorPos(0, 0), lines.endPos());
    auto p = params;
    p.maxResults = max(10, lines.totalPositions() / 4);

    TransformOptimizer opt(config);
    auto result = pureDeletionResult(opt, lines, boundary, p);

    ContextData ctx;
    for (const auto& l : lines) ctx.initialLines.push_back(l);
    // goalLines empty — pure deletion
    ctx.initialCursorLine = 0;
    ctx.initialCursorCol = 0;
    ctx.prefix = boundary.prefix();
    ctx.suffix = boundary.suffix();
    ctx.hasLinesAbove = boundary.hasLinesAbove();
    ctx.hasLinesBelow = boundary.hasLinesBelow();

    cases.push_back({ec.name, result.getStats(), collectEditResults(result), std::move(ctx)});
  }

  // Multi-line edit case
  {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines buffer = {"I saw a pig in barn in Switzerland", "Inconspicuous, even"};
    Lines goal = {"Florida"};
    TransformBoundary boundary(buffer, CursorPos(0, 23), CursorPos(1, 19));
    Lines editRegion = buffer.getSpan(CursorPos(0, 23), CursorPos(1, 19));
    auto p = params;
    p.maxResults = max(10, editRegion.totalPositions() / 4);

    TransformOptimizer opt(config);
    auto result = opt.optimizeTransform(editRegion, goal, boundary, p);

    ContextData ctx;
    for (const auto& l : editRegion) ctx.initialLines.push_back(l);
    for (const auto& l : goal) ctx.goalLines.push_back(l);
    ctx.initialCursorLine = 0;
    ctx.initialCursorCol = 0;
    ctx.goalCursorLine = 0;
    ctx.goalCursorCol = 0;
    ctx.prefix = boundary.prefix();
    ctx.suffix = boundary.suffix();
    ctx.hasLinesAbove = boundary.hasLinesAbove();
    ctx.hasLinesBelow = boundary.hasLinesBelow();

    cases.push_back({"MultiLineEdit/2L->1w", result.getStats(), collectEditResults(result), std::move(ctx)});
  }

  return cases;
}
