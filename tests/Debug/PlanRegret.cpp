// Plan-regret harness: does VimDiff's partition beat the alternative the
// composition optimizer could have used? For each catalog case x seed, run the
// full CompositionOptimizer over the VimDiff plan and over the Myers partition
// via the forcedDiffs seam, and compare real costs. An *inversion* is Myers
// achieving a strictly lower real cost; *regret* is real(VimDiff) - real(Myers)
// when positive. Debug-tier: these numbers guide cost calibration; nothing
// gates on them.
//
//   VIMFY_REGRET_SEEDS=8   seeds per catalog case
//
// Inversion lines show each partition's region count as [rN] next to its costs.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="PlanRegret.*"

#include <gtest/gtest.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/DiffPlanner/MyersDiff.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Utils/OptimizerCaseCatalog.h"
#include "Utils/PrettyText.h"

using namespace std;

namespace {

constexpr double NO_RESULT = numeric_limits<double>::max();

int envInt(const char* name, int fallback) {
  const char* v = getenv(name);
  return v ? atoi(v) : fallback;
}

const Result* bestResult(const CompositionResult& res) {
  const Result* best = nullptr;
  for (const Result& r : res.getResults())
    if (!best || r.getCost() < best->getCost()) best = &r;
  return best;
}

string signature(const vector<DiffState>& diffs) {
  string sig;
  for (const DiffState& d : diffs)
    sig += to_string(d.beginPos.line) + ":" + to_string(d.beginPos.col) + ":" + d.deletedText +
           ">" + d.insertedText + "|";
  return sig;
}

double realCost(const Config& config, const CompositionSetup& s,
                const vector<DiffState>& diffs) {
  CompositionOptimizer opt(config);
  auto res = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, s.params, "",
                          NavBoundary(), NavContext(), nullptr, &diffs);
  const Result* best = bestResult(res);
  return best ? best->getCost() : NO_RESULT;
}

}  // namespace

TEST(PlanRegret, Catalog) {
  const Config config = Config::uniform();
  const int seeds = envInt("VIMFY_REGRET_SEEDS", 8);

  int cases = 0, samePartition = 0, comparable = 0, inversions = 0, wins = 0, failures = 0;
  double regretSum = 0.0, regretMax = 0.0;

  cout << fixed << setprecision(2);
  for (const CompositionCaseSpec& spec : compositionCaseCatalog()) {
    for (int seed = 0; seed < seeds; seed++) {
      CompositionSetup s = buildCompositionSetup(spec, seed);
      VimDiff::Plan plan = VimDiff::calculate(
          s.initial, s.goal, config,
          VimDiff::CostOptions{.moveDeleteScale = s.params.moveDeleteScale});
      vector<DiffState> myers = MyersDiff::calculate(s.initial, s.goal);
      cases++;
      if (signature(plan.diffs) == signature(myers)) {
        samePartition++;
        continue;
      }

      const double realVim = realCost(config, s, plan.diffs);
      const double realMyers = realCost(config, s, myers);
      if (realVim == NO_RESULT || realMyers == NO_RESULT) {
        failures++;
        cout << "FAIL " << spec.name << " seed " << seed << ": no composition result for"
             << (realVim == NO_RESULT ? " vimdiff" : "") << (realMyers == NO_RESULT ? " myers" : "")
             << "\n";
        continue;
      }

      comparable++;
      const double regret = realVim - realMyers;
      if (regret > 1e-9) {
        inversions++;
        regretSum += regret;
        regretMax = max(regretMax, regret);
        cout << "INVERT " << spec.name << " seed " << seed << ": vimdiff[r" << plan.diffs.size()
             << "] est=" << plan.cost << " real=" << realVim << "  myers[r" << myers.size()
             << "] real=" << realMyers << "  regret=" << regret << "\n";
      } else if (regret < -1e-9) {
        wins++;
      }
    }
  }

  cout << "\n=== PlanRegret summary (seeds=" << seeds << ") ===\n"
       << "cases:          " << cases << " (" << samePartition << " same partition, "
       << failures << " failed)\n"
       << "comparable:     " << comparable << "\n"
       << "vimdiff wins:   " << wins << "\n"
       << "inversions:     " << inversions;
  if (comparable > 0) cout << " (" << 100.0 * inversions / comparable << "%)";
  cout << "\nmean regret:    " << (inversions ? regretSum / inversions : 0.0)
       << "\nmax regret:     " << regretMax << "\n";
}

// Dump one catalog case: the VimDiff plan and the Myers partition, each with its
// planner/real cost and best real sequence. For reading an inversion by hand.
//   VIMFY_DUMP_CASE=EditCount/8 VIMFY_DUMP_SEED=3
// Run: ./build/tests/vimfy_debug --gtest_filter="PlanRegret.DumpCase"
TEST(PlanRegret, DumpCase) {
  const char* caseName = getenv("VIMFY_DUMP_CASE");
  if (!caseName) GTEST_SKIP() << "set VIMFY_DUMP_CASE";
  const int seed = envInt("VIMFY_DUMP_SEED", 0);
  const Config config = Config::uniform();
  for (const CompositionCaseSpec& spec : compositionCaseCatalog()) {
    if (spec.name != caseName) continue;
    CompositionSetup s = buildCompositionSetup(spec, seed);
    cout << "initial:\n" << VF::prettify(s.initial.flatten()) << "\ngoal:\n"
         << VF::prettify(s.goal.flatten()) << "\n";
    VimDiff::Plan plan = VimDiff::calculate(
        s.initial, s.goal, config,
        VimDiff::CostOptions{.moveDeleteScale = s.params.moveDeleteScale});
    auto show = [&](const char* label, double est, const vector<DiffState>& diffs) {
      CompositionOptimizer opt(config);
      auto res = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, s.params, "",
                              NavBoundary(), NavContext(), nullptr, &diffs);
      const Result* best = bestResult(res);
      cout << label << " est=" << est << " real=" << (best ? best->getCost() : -1.0)
           << " seq=" << (best ? best->getSequence().str() : "<none>") << "\n";
      for (const DiffState& d : diffs)
        cout << "   (" << d.beginPos.line << "," << d.beginPos.col << ") del=["
             << VF::prettify(d.deletedText) << "] ins=[" << VF::prettify(d.insertedText)
             << "]\n";
    };
    show("vimdiff", plan.cost, plan.diffs);
    show("myers", -1.0, MyersDiff::calculate(s.initial, s.goal));
  }
}
