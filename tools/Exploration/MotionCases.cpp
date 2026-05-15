#include "Exploration/ExplorationCollector.h"

#include "Boundary/NavBoundary.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"
#include "Utils/SeedManager.h"

using namespace std;

vector<ExploreCase> collectMotionCases() {
  vector<ExploreCase> cases;
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

  NavOptimizerParams params;
  for (const auto& mc : motionCases) {
    RandomGen::seed(seedMgr.getSeed(0));
    Lines lines = randomCodeBuffer(mc.numLines, mc.avgLen);
    CursorPos firstPos = randomFirstPos(lines);
    CursorPos lastPos = randomLastPos(lines);
    CursorPos boundaryEnd(lastPos.line, lastPos.col + 1);
    NavBoundary boundary(lines, firstPos, boundaryEnd, true, true);

    NavOptimizer opt(config);
    auto result = opt.optimize(lines, firstPos, lastPos, params, "", boundary);

    vector<FoundResult> found;
    for (const auto& r : result.getResults()) {
      if (!r.getSequence().empty()) {
        found.push_back({r.getSequence().str(), r.getCost()});
      }
    }

    ContextData ctx;
    for (const auto& l : lines) ctx.initialLines.push_back(l);
    ctx.goalLines = ctx.initialLines;
    ctx.initialCursorLine = firstPos.line;
    ctx.initialCursorCol = firstPos.col;
    ctx.goalCursorLine = lastPos.line;
    ctx.goalCursorCol = lastPos.col;
    ctx.hasLinesAbove = true;
    ctx.hasLinesBelow = true;

    cases.push_back({mc.name, result.getStats(), std::move(found), std::move(ctx)});
  }

  return cases;
}
