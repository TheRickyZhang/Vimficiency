// Plan-regret harness: does VimDiff's plan ranking match the real composed
// optimizer? For each catalog case x seed, take the planner's top-K partitions,
// run the full CompositionOptimizer over each via the forcedDiffs seam, and
// compare. An *inversion* is plan 1 not achieving the best real cost; *regret*
// is real(plan 1) - min_k real(plan k). Debug-tier: these numbers guide cost
// calibration; nothing gates on them.
//
//   VIMFY_REGRET_SEEDS=8   seeds per catalog case
//   VIMFY_REGRET_PLANS=4   K
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

using namespace std;

namespace {

constexpr double NO_RESULT = numeric_limits<double>::max();

int envInt(const char* name, int fallback) {
  const char* v = getenv(name);
  return v ? atoi(v) : fallback;
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
  int pairs = 0, concordant = 0;
  double regretSum = 0.0, regretMax = 0.0;

  cout << fixed << setprecision(2);
  for (const CompositionCaseSpec& spec : compositionCaseCatalog()) {
    for (int seed = 0; seed < seeds; seed++) {
      CompositionSetup s = buildCompositionSetup(spec, seed);
      vector<VimDiff::Plan> plans = VimDiff::calculate(
          s.initial, s.goal, config,
          VimDiff::CostOptions{
              .diffOpenPenalty = s.params.diffOpenPenalty,
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
      if (regret > 1e-9) {
        inversions++;
        cout << "INVERT " << spec.name << " seed " << seed << ":";
        for (int p = 0; p < (int)real.size(); p++)
          cout << "  plan" << (p + 1) << " est=" << plans[p].cost
               << " real=" << real[p] << (p == argmin ? "*" : "");
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
       << "\nrank pairs:  " << concordant << "/" << pairs << " concordant";
  if (pairs > 0) cout << " (" << 100.0 * concordant / pairs << "%)";
  cout << "\n";
}
