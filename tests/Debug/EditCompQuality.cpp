#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizer.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"
#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

namespace {

constexpr int NUM_SEEDS = 5;
constexpr int DIJKSTRA_POP_CAP = 8000;  // bound the baseline; capped runs are skipped
constexpr double EPS = 1e-9;
const double INF = numeric_limits<double>::max();

Config cfg() { return Config::uniform(); }

bool capped(SearchStopReason r) { return r == SearchStopReason::MaxPopsReached; }

double bucketMin(span<const Result> bucket) {
  double best = INF;
  for (const auto& res : bucket) best = min(best, res.getCost());
  return best;
}

TransformResult runEdit(const EditSetup& s, const TransformOptimizerParams& p) {
  TransformOptimizer opt(cfg());
  if (s.goalKind == EditGoalKind::PureDeletion)
    return opt.optimizePureDeletion(s.initialLines, s.boundary, p);
  return opt.optimizeTransform(s.initialLines, s.goalLines, s.boundary, p);
}

}  // namespace

// Per-start-index cost comparison: heuristic vs pure-effort Dijkstra.
TEST(EditCompQuality, DISABLED_Edit) {
  printf("%-22s %6s %7s %9s %9s %7s\n",
         "case", "cmps", "%worse", "meanGap%", "maxGap%", "dijCap");
  long gCmp = 0, gWorse = 0;
  double gSum = 0, gMax = 0;

  for (const auto& spec : editCaseCatalog()) {
    long cCmp = 0, cWorse = 0;
    double cSum = 0, cMax = 0;
    int cCap = 0;

    for (int seed = 0; seed < NUM_SEEDS; ++seed) {
      EditSetup s = buildEditSetup(spec, seed);
      TransformOptimizerParams dp = s.params;
      dp.withDistanceWeight(0.0);
      dp.withMaxNodesPopped(DIJKSTRA_POP_CAP);
      auto hRes = runEdit(s, s.params);
      auto dRes = runEdit(s, dp);
      if (capped(dRes.getStats().stopReason())) { cCap++; continue; }

      const auto& hb = hRes.getResults();
      const auto& db = dRes.getResults();
      size_t n = min(hb.size(), db.size());
      for (size_t i = 0; i < n; ++i) {
        if (hb[i].empty() || db[i].empty()) continue;
        double hc = bucketMin({hb[i].data(), hb[i].size()});
        double dc = bucketMin({db[i].data(), db[i].size()});
        if (dc <= EPS) continue;
        double rel = (hc - dc) / dc;
        cCmp++; cSum += rel; cMax = max(cMax, rel);
        gCmp++; gSum += rel; gMax = max(gMax, rel);
        if (hc > dc + EPS) { cWorse++; gWorse++; }
      }
    }
    printf("%-22s %6ld %6.1f%% %8.1f%% %8.1f%% %7d\n", spec.name.c_str(), cCmp,
           cCmp ? 100.0 * cWorse / cCmp : 0.0,
           cCmp ? 100.0 * cSum / cCmp : 0.0, 100.0 * cMax, cCap);
  }
  printf("OVERALL edit: cmps=%ld worse=%ld(%.1f%%) meanGap=%.2f%% maxGap=%.2f%%\n",
         gCmp, gWorse, gCmp ? 100.0 * gWorse / gCmp : 0.0,
         gCmp ? 100.0 * gSum / gCmp : 0.0, 100.0 * gMax);
}

TEST(EditCompQuality, DISABLED_Composition) {
  printf("%-22s %6s %7s %9s %9s %7s\n",
         "case", "cmps", "%worse", "meanGap%", "maxGap%", "dijCap");
  long gCmp = 0, gWorse = 0;
  double gSum = 0, gMax = 0;

  for (const auto& spec : compositionCaseCatalog()) {
    long cCmp = 0, cWorse = 0;
    double cSum = 0, cMax = 0;
    int cCap = 0;

    for (int seed = 0; seed < NUM_SEEDS; ++seed) {
      CompositionSetup s = buildCompositionSetup(spec, seed);
      CompositionOptimizer opt(cfg());
      CompositionOptimizerParams dp = s.params;
      dp.withDistanceWeight(0.0);
      dp.withMaxNodesPopped(DIJKSTRA_POP_CAP);
      auto hRes = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, s.params);
      auto dRes = opt.optimize(s.initial, {0, 0}, s.goal, {0, 0}, dp);
      if (capped(dRes.getStats().stopReason())) { cCap++; continue; }
      if (hRes.getResults().empty() || dRes.getResults().empty()) continue;

      double hc = bucketMin({hRes.getResults().data(), hRes.getResults().size()});
      double dc = bucketMin({dRes.getResults().data(), dRes.getResults().size()});
      if (dc <= EPS) continue;
      double rel = (hc - dc) / dc;
      cCmp++; cSum += rel; cMax = max(cMax, rel);
      gCmp++; gSum += rel; gMax = max(gMax, rel);
      if (hc > dc + EPS) { cWorse++; gWorse++; }
    }
    printf("%-22s %6ld %6.1f%% %8.1f%% %8.1f%% %7d\n", spec.name.c_str(), cCmp,
           cCmp ? 100.0 * cWorse / cCmp : 0.0,
           cCmp ? 100.0 * cSum / cCmp : 0.0, 100.0 * cMax, cCap);
  }
  printf("OVERALL comp: cmps=%ld worse=%ld(%.1f%%) meanGap=%.2f%% maxGap=%.2f%%\n",
         gCmp, gWorse, gCmp ? 100.0 * gWorse / gCmp : 0.0,
         gCmp ? 100.0 * gSum / gCmp : 0.0, 100.0 * gMax);
}
