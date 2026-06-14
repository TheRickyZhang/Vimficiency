#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Types/CharInterval.h"
#include "Types/CharRange.h"
#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

namespace {

constexpr int NUM_SEEDS = 40;
constexpr double EPS = 1e-9;

LandingNavResult runCase(const NavSetup& s, const NavOptimizerParams& params) {
  NavOptimizer opt(Config::uniform());
  if (s.goalKind == NavGoalKind::Point) {
    return opt.optimize(s.lines, s.start, s.goalPoint, params, "", s.boundary);
  }
  return opt.optimize(
      s.lines, s.start, CharInterval(CharRange(s.rangeBegin, s.rangeEnd), s.lines),
      params, "", s.boundary);
}

map<pair<int, int>, double> bestByPos(const vector<LandingResult>& results) {
  map<pair<int, int>, double> m;
  for (const auto& r : results) {
    auto key = make_pair(r.getGoalPos().line, r.getGoalPos().col);
    auto it = m.find(key);
    if (it == m.end() || r.getCost() < it->second) m[key] = r.getCost();
  }
  return m;
}

string seqAtPos(const LandingNavResult& res, pair<int, int> pos) {
  const LandingResult* best = nullptr;
  for (const auto& r : res.getResults()) {
    if (make_pair(r.getGoalPos().line, r.getGoalPos().col) != pos) continue;
    if (!best || r.getCost() < best->getCost()) best = &r;
  }
  return best ? best->getSequence().str() : "";
}

struct Example {
  string name;
  int seed;
  double stdCost, dijCost;
  string stdSeq, dijSeq;
  double relGap;
};

}  // namespace

TEST(NavQuality, DISABLED_StandardVsDijkstra) {
  const auto& catalog = navCaseCatalog();

  long totalCmp = 0, stdWorse = 0, stdBetter = 0, equalCmp = 0;
  long stdMissed = 0;  // positions Dijkstra found that Standard didn't
  double sumRelAll = 0, sumRelWorse = 0, maxRel = 0;
  double gStdPops = 0, gDijPops = 0;
  vector<Example> worst;

  printf("%-22s %6s %7s %9s %9s %8s %8s\n",
         "case", "cmps", "%worse", "meanGap%", "maxGap%", "stdPops", "dijPops");

  for (const auto& spec : catalog) {
    long cCmp = 0, cWorse = 0;
    double cSumRel = 0, cMaxRel = 0;
    double cStdPops = 0, cDijPops = 0;

    for (int seed = 0; seed < NUM_SEEDS; ++seed) {
      NavSetup s = buildNavSetup(spec, seed);
      NavOptimizerParams dijParams = spec.params;
      dijParams.withDistanceWeight(0.0);
      auto stdRes = runCase(s, spec.params);
      auto dijRes = runCase(s, dijParams);
      cStdPops += stdRes.getStats().totalPops();
      cDijPops += dijRes.getStats().totalPops();

      auto stdMap = bestByPos(stdRes.getResults());
      auto dijMap = bestByPos(dijRes.getResults());

      for (const auto& [pos, dijCost] : dijMap) {
        auto it = stdMap.find(pos);
        if (it == stdMap.end()) {
          stdMissed++;
          continue;
        }
        double stdCost = it->second;
        double rel = dijCost > EPS ? (stdCost - dijCost) / dijCost : 0.0;
        totalCmp++;
        cCmp++;
        sumRelAll += rel;
        cSumRel += rel;
        if (stdCost > dijCost + EPS) {
          stdWorse++;
          cWorse++;
          sumRelWorse += rel;
          cMaxRel = max(cMaxRel, rel);
          maxRel = max(maxRel, rel);
          worst.push_back({spec.name, seed, stdCost, dijCost,
                           seqAtPos(stdRes, pos), seqAtPos(dijRes, pos), rel});
        } else if (stdCost < dijCost - EPS) {
          stdBetter++;  // should not happen: Dijkstra is optimal
        } else {
          equalCmp++;
        }
      }
    }

    gStdPops += cStdPops;
    gDijPops += cDijPops;
    printf("%-22s %6ld %6.1f%% %8.1f%% %8.1f%% %8.1f %8.1f\n",
           spec.name.c_str(), cCmp,
           cCmp ? 100.0 * cWorse / cCmp : 0.0,
           cCmp ? 100.0 * cSumRel / cCmp : 0.0, 100.0 * cMaxRel,
           cStdPops / NUM_SEEDS, cDijPops / NUM_SEEDS);
  }

  printf("\n=== OVERALL ===\n");
  printf("comparisons:            %ld\n", totalCmp);
  printf("std strictly worse:     %ld (%.1f%%)\n", stdWorse,
         totalCmp ? 100.0 * stdWorse / totalCmp : 0.0);
  printf("std equal to optimal:   %ld (%.1f%%)\n", equalCmp,
         totalCmp ? 100.0 * equalCmp / totalCmp : 0.0);
  printf("std better than 'opt':  %ld (pruning artifact if >0)\n", stdBetter);
  printf("positions std missed:   %ld\n", stdMissed);
  printf("mean gap (all cmps):    %.2f%%\n", totalCmp ? 100.0 * sumRelAll / totalCmp : 0.0);
  printf("mean gap (when worse):  %.2f%%\n", stdWorse ? 100.0 * sumRelWorse / stdWorse : 0.0);
  printf("max gap:                %.2f%%\n", 100.0 * maxRel);
  long nRuns = (long)catalog.size() * NUM_SEEDS;
  printf("mean pops std:          %.1f\n", gStdPops / nRuns);
  printf("mean pops dijkstra:     %.1f\n", gDijPops / nRuns);

  sort(worst.begin(), worst.end(),
       [](const Example& a, const Example& b) { return a.relGap > b.relGap; });
  printf("\n=== WORST CASES (std vs optimal) ===\n");
  for (size_t i = 0; i < worst.size() && i < 12; ++i) {
    const auto& e = worst[i];
    printf("  %-22s seed=%d  std=%.2f [%s]  dij=%.2f [%s]  +%.1f%%\n",
           e.name.c_str(), e.seed, e.stdCost, e.stdSeq.c_str(),
           e.dijCost, e.dijSeq.c_str(), 100.0 * e.relGap);
  }
}
