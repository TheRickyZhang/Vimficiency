// Plan-regret harness: does VimDiff's plan ranking match the real composed
// optimizer? For each catalog case x seed, take the planner's top-K partitions,
// run the full CompositionOptimizer over each via the forcedDiffs seam, and
// compare. An *inversion* is plan 1 not achieving the best real cost; *regret*
// is real(plan 1) - min_k real(plan k). The *tie-adjusted* variants treat every
// plan the planner priced equal to plan 1 as interchangeable (their order is
// generation order, not a ranking), so they measure the cost model alone and
// the raw numbers measure model + tie-break. Debug-tier: these numbers guide
// cost calibration; nothing gates on them.
//
//   VIMFY_REGRET_SEEDS=8   seeds per catalog case
//   VIMFY_REGRET_PLANS=4   K
//
// Inversion lines show each plan's region count as [rN] next to its costs.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="PlanRegret.*"

#include <gtest/gtest.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
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

double envDouble(const char* name, double fallback) {
  const char* v = getenv(name);
  return v ? atof(v) : fallback;
}

double realCost(const Config& config, const CompositionSetup& s,
                const vector<DiffState>& diffs) {
  CompositionOptimizer opt(config);
  auto res = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, s.params, "",
                          NavBoundary(), NavContext(), nullptr, &diffs);
  double best = NO_RESULT;
  for (const Result& r : res.getResults()) best = min(best, r.getCost());
  return best;
}

}  // namespace

TEST(PlanRegret, Catalog) {
  const Config config = Config::uniform();
  const int seeds = envInt("VIMFY_REGRET_SEEDS", 8);
  const int K = envInt("VIMFY_REGRET_PLANS", 4);

  int cases = 0, singlePlan = 0, comparable = 0, inversions = 0, failures = 0;
  int pairs = 0, concordant = 0, tiedInversions = 0;
  double regretSum = 0.0, regretMax = 0.0, tiedRegretSum = 0.0, tiedRegretMax = 0.0;

  cout << fixed << setprecision(2);
  for (const CompositionCaseSpec& spec : compositionCaseCatalog()) {
    for (int seed = 0; seed < seeds; seed++) {
      CompositionSetup s = buildCompositionSetup(spec, seed);
      vector<VimDiff::Plan> plans = VimDiff::calculate(
          s.initial, s.goal, config,
          VimDiff::CostOptions{
              .moveDeleteScale = s.params.moveDeleteScale,
              .maxPlans = K,
          });
      cases++;
      if ((int)plans.size() < 2) {
        singlePlan++;
        continue;
      }

      vector<double> real(plans.size());
      bool anyFail = false;
      for (int p = 0; p < (int)plans.size(); p++) {
        real[p] = realCost(config, s, plans[p].diffs);
        if (real[p] == NO_RESULT) anyFail = true;
      }
      if (anyFail) {
        failures++;
        cout << "FAIL " << spec.name << " seed " << seed
             << ": a plan produced no composition result";
        for (int p = 0; p < (int)real.size(); p++)
          if (real[p] == NO_RESULT) cout << "  plan" << (p + 1);
        cout << "\n";
        continue;
      }

      comparable++;
      int argmin = 0;
      for (int p = 1; p < (int)real.size(); p++)
        if (real[p] < real[argmin]) argmin = p;
      double regret = real[0] - real[argmin];
      regretSum += regret;
      regretMax = max(regretMax, regret);
      double tiedBest = real[0];
      for (int p = 1; p < (int)real.size(); p++)
        if (abs(plans[p].cost - plans[0].cost) < 1e-9) tiedBest = min(tiedBest, real[p]);
      const double tiedRegret = tiedBest - real[argmin];
      tiedRegretSum += tiedRegret;
      tiedRegretMax = max(tiedRegretMax, tiedRegret);
      if (tiedRegret > 1e-9) tiedInversions++;
      if (regret > 1e-9) {
        inversions++;
        cout << "INVERT " << spec.name << " seed " << seed << ":";
        for (int p = 0; p < (int)real.size(); p++)
          cout << "  plan" << (p + 1) << "[r" << plans[p].diffs.size() << "] est="
               << plans[p].cost << " real=" << real[p] << (p == argmin ? "*" : "");
        cout << "  regret=" << regret << "\n";
      }
      // Plans are ascending by planner cost, so pair (i, j<i...) order encodes
      // the planner's ranking; concordant = the real costs agree with it.
      for (int i = 0; i < (int)real.size(); i++)
        for (int j = i + 1; j < (int)real.size(); j++) {
          if (real[i] == real[j]) continue;
          pairs++;
          if (real[i] < real[j]) concordant++;
        }
    }
  }

  cout << "\n=== PlanRegret summary (K=" << K << ", seeds=" << seeds << ") ===\n"
       << "cases:       " << cases << " (" << singlePlan << " single-plan, "
       << failures << " failed)\n"
       << "comparable:  " << comparable << "\n"
       << "inversions:  " << inversions;
  if (comparable > 0) cout << " (" << 100.0 * inversions / comparable << "%)";
  cout << "\nmean regret: " << (comparable ? regretSum / comparable : 0.0)
       << "\nmax regret:  " << regretMax
       << "\ntie-adjusted inversions: " << tiedInversions;
  if (comparable > 0) cout << " (" << 100.0 * tiedInversions / comparable << "%)";
  cout << "\ntie-adjusted mean regret: " << (comparable ? tiedRegretSum / comparable : 0.0)
       << "\ntie-adjusted max regret:  " << tiedRegretMax
       << "\nrank pairs:  " << concordant << "/" << pairs << " concordant";
  if (pairs > 0) cout << " (" << 100.0 * concordant / pairs << "%)";
  cout << "\n";
}

// Dump one catalog case: each K-best plan's regions, planner cost, real
// composition cost and best real sequence. For reading an inversion by hand.
//   VIMFY_DUMP_CASE=EditCount/8 VIMFY_DUMP_SEED=3
// Run: ./build/tests/vimfy_debug --gtest_filter="PlanRegret.DumpCase"
TEST(PlanRegret, DumpCase) {
  const char* caseName = getenv("VIMFY_DUMP_CASE");
  if (!caseName) GTEST_SKIP() << "set VIMFY_DUMP_CASE";
  const int seed = envInt("VIMFY_DUMP_SEED", 0);
  const int K = envInt("VIMFY_REGRET_PLANS", 4);
  const Config config = Config::uniform();
  for (const CompositionCaseSpec& spec : compositionCaseCatalog()) {
    if (spec.name != caseName) continue;
    CompositionSetup s = buildCompositionSetup(spec, seed);
    cout << "initial:\n" << VF::prettify(s.initial.flatten()) << "\ngoal:\n"
         << VF::prettify(s.goal.flatten()) << "\n";
    vector<VimDiff::Plan> plans = VimDiff::calculate(
        s.initial, s.goal, config,
        VimDiff::CostOptions{.moveDeleteScale = s.params.moveDeleteScale, .maxPlans = K});
    for (int p = 0; p < (int)plans.size(); p++) {
      CompositionOptimizer opt(config);
      auto res = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, s.params, "",
                              NavBoundary(), NavContext(), nullptr, &plans[p].diffs);
      const Result* best = nullptr;
      for (const Result& r : res.getResults())
        if (!best || r.getCost() < best->getCost()) best = &r;
      cout << "plan" << (p + 1) << " est=" << plans[p].cost
           << " real=" << (best ? best->getCost() : -1.0) << " seq="
           << (best ? best->getSequence().str() : "<none>") << "\n";
      for (const DiffState& d : plans[p].diffs)
        cout << "   (" << d.beginPos.line << "," << d.beginPos.col << ") del=["
             << VF::prettify(d.deletedText) << "] ins=[" << VF::prettify(d.insertedText)
             << "]\n";
    }
  }
}
