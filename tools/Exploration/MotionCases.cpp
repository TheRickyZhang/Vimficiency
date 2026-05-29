#include "Exploration/ExplorationCollector.h"

#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Types/CharInterval.h"
#include "Types/CharRange.h"
#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

vector<ExploreCase> collectMotionCases() {
  vector<ExploreCase> cases;

  for (const auto& spec : navCaseCatalog()) {
    // Seed 0 is the representative instance of the case the benchmark times
    // across DEFAULT_SEED_COUNT seeds; buildNavSetup fixes the identical buffer.
    NavSetup s = buildNavSetup(spec, 0);

    NavOptimizer opt(config);
    LandingNavResult result =
        spec.goalKind == NavGoalKind::Point
            ? opt.optimize(s.lines, s.start, s.goalPoint, spec.params, "", s.boundary)
            : opt.optimize(
                  s.lines, s.start,
                  CharInterval(CharRange(s.rangeBegin, s.rangeEnd), s.lines),
                  spec.params, "", s.boundary);

    vector<FoundResult> found;
    for (const auto& r : result.getResults()) {
      if (!r.getSequence().empty()) {
        found.push_back({r.getSequence().str(), r.getCost()});
      }
    }

    ContextData ctx;
    for (const auto& l : s.lines) ctx.initialLines.push_back(l);
    ctx.goalLines = ctx.initialLines;  // motion never changes text
    ctx.initialCursorLine = s.start.line;
    ctx.initialCursorCol = s.start.col;
    ctx.hasLinesAbove = true;
    ctx.hasLinesBelow = true;
    if (spec.goalKind == NavGoalKind::Point) {
      ctx.goalCursorLine = s.goalPoint.line;
      ctx.goalCursorCol = s.goalPoint.col;
    } else {
      ctx.goalRangeBeginLine = s.rangeBegin.line;
      ctx.goalRangeBeginCol = s.rangeBegin.col;
      ctx.goalRangeEndLine = s.rangeEnd.line;
      ctx.goalRangeEndCol = s.rangeEnd.col;
    }

    cases.push_back({spec.name, result.getStats(), std::move(found), std::move(ctx)});
  }

  return cases;
}
