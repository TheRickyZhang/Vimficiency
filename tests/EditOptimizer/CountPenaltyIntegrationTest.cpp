#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "Optimizer/Config.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/EditOptimizer/EditExplorer.h"
#include "Optimizer/EditOptimizer/EditOptimizerParams.h"
#include "Optimizer/EditOptimizer/EditSearchContext.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "VimTypes/Lines.h"

namespace {
struct RuntimeOptionsGuard {
  GlobalRuntimeOptions saved;
  RuntimeOptionsGuard() : saved(globalRuntimeOptions()) {}
  ~RuntimeOptionsGuard() { globalRuntimeOptions() = saved; }
};
}  // namespace

TEST(EditCountPenaltyIntegrationTest, CountedLineEditsIncludeRuntimePenalty) {
  RuntimeOptionsGuard guard;

  Config config = Config::uniform();
  Lines lines = {"a", "b", "c", "d"};
  EditBoundary boundary(lines, Position(0, 0), lines.endPos());
  EditOptimizerParams params = EditOptimizerParams{}.withMinCountRepeat(2);
  EditSearchContext ctx(lines, boundary, params, config);
  EditExplorer explorer(ctx);
  Position cursor(0, 0);

  double baseline = -1.0;
  explorer.exploreCountedLineEdits(
      cursor, lines, 2,
      [&](LineRange, const SequenceBinding& sourceCmd) {
        if (sourceCmd.count == 3 && sourceCmd.base.seq.view() == "dd") {
          baseline = sourceCmd.effort.getEffort(config);
        }
      });
  ASSERT_GT(baseline, 0.0);

  auto& options = globalRuntimeOptions();
  options.useCountPenaltyOverrides = true;
  options.countPenaltyOverrides = {};
  PartialCountPenaltyParams o;
  o.base = 30.0;
  o.countSlope = 0.0;
  o.spanSlope = 0.0;
  options.countPenaltyOverrides[toIndex(CountClass::EditLine)] = o;

  double overridden = -1.0;
  explorer.exploreCountedLineEdits(
      cursor, lines, 2,
      [&](LineRange, const SequenceBinding& sourceCmd) {
        if (sourceCmd.count == 3 && sourceCmd.base.seq.view() == "dd") {
          overridden = sourceCmd.effort.getEffort(config);
        }
      });
  ASSERT_GT(overridden, 0.0);
  EXPECT_GT(overridden, baseline + 20.0);
}

TEST(EditCountPenaltyIntegrationTest, CountedJoinEditsIncludeRuntimePenalty) {
  RuntimeOptionsGuard guard;

  Config config = Config::uniform();
  Lines lines = {"a", "b", "c", "d"};
  EditBoundary boundary(lines, Position(0, 0), lines.endPos());
  EditOptimizerParams params = EditOptimizerParams{}.withMinCountRepeat(2);
  EditSearchContext ctx(lines, boundary, params, config);
  EditExplorer explorer(ctx);
  Position cursor(0, 0);

  double baseline = -1.0;
  explorer.exploreCountedJoinCommands(
      cursor, lines, 2,
      [&](bool addSpace, const SequenceBinding& sourceCmd) {
        if (!addSpace && sourceCmd.count == 3 && sourceCmd.base.seq.view() == "gJ") {
          baseline = sourceCmd.effort.getEffort(config);
        }
      });
  ASSERT_GT(baseline, 0.0);

  auto& options = globalRuntimeOptions();
  options.useCountPenaltyOverrides = true;
  options.countPenaltyOverrides = {};
  PartialCountPenaltyParams o;
  o.base = 25.0;
  o.countSlope = 0.0;
  o.spanSlope = 0.0;
  options.countPenaltyOverrides[toIndex(CountClass::Join)] = o;

  double overridden = -1.0;
  explorer.exploreCountedJoinCommands(
      cursor, lines, 2,
      [&](bool addSpace, const SequenceBinding& sourceCmd) {
        if (!addSpace && sourceCmd.count == 3 && sourceCmd.base.seq.view() == "gJ") {
          overridden = sourceCmd.effort.getEffort(config);
        }
      });
  ASSERT_GT(overridden, 0.0);
  EXPECT_GT(overridden, baseline + 15.0);
}
