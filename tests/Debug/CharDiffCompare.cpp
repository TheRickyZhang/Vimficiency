// tests/Debug/CharDiffCompare.cpp
//
// End-to-end regression comparison for promoting CharDiff (diffAlgorithm=2) to
// default. Runs the full CompositionOptimizer with Tree vs Char on realistic
// edits and reports best-result cost (quality) and diff-generation+search time
// (speed). A "regression" is Char producing a worse best cost than Tree.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="CharDiffCompare.*"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/CharDiff.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/DiffPlanner/TreeDiff.h"
#include "Types/Lines.h"

using namespace std;

namespace {

struct CompareCase {
  string name;
  Lines initial;
  Lines goal;
};

struct Outcome {
  double bestCost = 1e18;
  int results = 0;
  double micros = 0.0;
  string bestSeq;
};

Outcome runOne(const Lines& initial, const Lines& goal, int algorithm) {
  Config config = Config::uniform();
  CompositionOptimizerParams params;
  params.diffAlgorithm = algorithm;

  // Warm + timed: a few reps, take the min wall time to cut noise.
  Outcome out;
  for (int rep = 0; rep < 5; rep++) {
    CompositionOptimizer opt(config);
    auto t0 = chrono::steady_clock::now();
    CompositionResult result = opt.optimize(initial, {0, 0}, goal, {0, 0}, params);
    auto t1 = chrono::steady_clock::now();
    double us = chrono::duration<double, micro>(t1 - t0).count();
    out.micros = (rep == 0) ? us : min(out.micros, us);
    out.results = static_cast<int>(result.resultCount());
    double best = 1e18;
    const Result* bestR = nullptr;
    for (const Result& r : result.getResults())
      if (r.getCost() < best) { best = r.getCost(); bestR = &r; }
    out.bestCost = best;
    if (bestR) { ostringstream oss; oss << bestR->getSequence(); out.bestSeq = oss.str(); }
  }
  return out;
}

}  // namespace

TEST(CharDiffCompare, TreeVsChar) {
  vector<CompareCase> cases = {
      {"rename-ident", {"int x = foo(bar, baz);"}, {"int y = foo(bar, baz);"}},
      {"rename-arg", {"int x = foo(bar, baz);"}, {"int x = foo(qux, baz);"}},
      {"prose-word", {"the quick brown fox"}, {"the slow brown fox"}},
      {"append-word", {"hello world"}, {"hello world again"}},
      {"delete-trailing", {"hello world again"}, {"hello world"}},
      {"insert-mid", {"abcdef"}, {"abcXYdef"}},
      {"two-edits", {"aaa bbb ccc"}, {"xxx bbb yyy"}},
      {"join-lines", {"first line", "second line"}, {"first line second line"}},
      {"split-line", {"alpha beta gamma"}, {"alpha", "beta gamma"}},
      {"multiline-edit",
       {"def process(data):", "    return data + 1"},
       {"def process(items):", "    return items + 2"}},
      {"delete-line", {"line one", "line two", "line three"}, {"line one", "line three"}},
      {"insert-line", {"line one", "line three"}, {"line one", "line two", "line three"}},
      {"paragraph-edit",
       {"first para here", "", "second para text"},
       {"first para here", "", "second para changed"}},
      {"indent-change", {"    foo();"}, {"        foo();"}},
      {"big-rename",
       {"result = compute(alpha, beta, gamma)"},
       {"output = compute(alpha, beta, gamma)"}},
  };

  cout << "\n" << left << setw(18) << "case"
       << right << setw(11) << "tree$" << setw(11) << "char$"
       << setw(9) << "d$" << setw(11) << "tree_us" << setw(11) << "char_us"
       << setw(9) << "speedup" << "\n";
  cout << string(80, '-') << "\n";

  int regressions = 0, improvements = 0;
  double treeCostSum = 0, charCostSum = 0, treeTimeSum = 0, charTimeSum = 0;
  for (const CompareCase& c : cases) {
    Outcome tree = runOne(c.initial, c.goal, DiffAlgorithm::Tree);
    Outcome chr = runOne(c.initial, c.goal, DiffAlgorithm::Char);
    double dCost = chr.bestCost - tree.bestCost;
    if (dCost > 1e-6) regressions++;
    if (dCost < -1e-6) improvements++;
    treeCostSum += tree.bestCost;
    charCostSum += chr.bestCost;
    treeTimeSum += tree.micros;
    charTimeSum += chr.micros;

    cout << left << setw(18) << c.name << right << fixed << setprecision(2)
         << setw(11) << tree.bestCost << setw(11) << chr.bestCost
         << setw(9) << dCost << setw(11) << tree.micros << setw(11) << chr.micros
         << setw(8) << setprecision(2) << (chr.micros > 0 ? tree.micros / chr.micros : 0) << "x"
         << (dCost > 1e-6 ? "  REGRESSION" : dCost < -1e-6 ? "  better" : "")
         << "\n";
  }
  cout << string(80, '-') << "\n";
  cout << "regressions (char worse): " << regressions << " / " << cases.size() << "\n"
       << "improvements (char better): " << improvements << "\n"
       << "total cost  tree=" << treeCostSum << "  char=" << charCostSum << "\n"
       << "total time  tree=" << treeTimeSum << "us  char=" << charTimeSum << "us\n";
}

// Broader randomized end-to-end batch: best-cost only (one run each), aggregate
// regression stats over realistic structured edits.
TEST(CharDiffCompare, RandomBatch) {
  mt19937 rng(31);
  auto word = [&]() {
    int len = 2 + (int)(rng() % 5);
    string w;
    for (int k = 0; k < len; k++) w += char('a' + rng() % 8);
    return w;
  };
  auto buffer = [&]() {
    int lines = 1 + (int)(rng() % 3);
    Lines b;
    for (int l = 0; l < lines; l++) {
      int words = 1 + (int)(rng() % 4);
      string s;
      for (int w = 0; w < words; w++) { if (w) s += ' '; s += word(); }
      b.push_back(s);
    }
    return b;
  };
  auto mutate = [&](Lines lines) {
    int edits = 1 + (int)(rng() % 3);
    for (int e = 0; e < edits; e++) {
      int li = rng() % lines.size();
      string& s = lines[li];
      int op = rng() % 3;
      if (op == 0 && !s.empty()) s[rng() % s.size()] = char('a' + rng() % 8);
      else if (op == 1) s.insert(s.begin() + (rng() % (s.size() + 1)), char('a' + rng() % 8));
      else if (!s.empty()) s.erase(s.begin() + (rng() % s.size()));
    }
    return lines;
  };

  struct Worst { Lines initial, goal; double treeCost, charCost; string treeSeq, charSeq; };
  vector<Worst> worst;

  int tested = 0, regressions = 0, improvements = 0;
  double treeSum = 0, charSum = 0, maxRegret = 0;
  for (int it = 0; it < 250; it++) {
    Lines initial = buffer();
    Lines goal = mutate(initial);
    if (initial.flatten() == goal.flatten()) continue;
    tested++;
    Outcome tree = runOne(initial, goal, DiffAlgorithm::Tree);
    Outcome chr = runOne(initial, goal, DiffAlgorithm::Char);
    treeSum += tree.bestCost;
    charSum += chr.bestCost;
    double d = chr.bestCost - tree.bestCost;
    if (d > 1e-6) {
      regressions++;
      maxRegret = max(maxRegret, d);
      worst.push_back({initial, goal, tree.bestCost, chr.bestCost, tree.bestSeq, chr.bestSeq});
    }
    if (d < -1e-6) improvements++;
  }
  cout << "\n=== Random end-to-end batch (Tree vs Char best cost) ===\n"
       << "cases:        " << tested << "\n"
       << "char worse:   " << regressions << " (" << (100.0 * regressions / tested) << "%)\n"
       << "char better:  " << improvements << " (" << (100.0 * improvements / tested) << "%)\n"
       << "max regret:   " << maxRegret << " keystrokes\n"
       << "total cost    tree=" << treeSum << "  char=" << charSum
       << "  (" << (100.0 * (charSum - treeSum) / treeSum) << "%)\n";

  sort(worst.begin(), worst.end(),
       [](const Worst& a, const Worst& b) { return (a.charCost - a.treeCost) > (b.charCost - b.treeCost); });
  Config config = Config::uniform();
  cout << "\n--- worst regressions (diff partitions) ---\n";
  for (int k = 0; k < (int)worst.size() && k < 6; k++) {
    const Worst& w = worst[k];
    cout << "\n[" << k << "] \"" << w.initial.flatten() << "\" -> \"" << w.goal.flatten()
         << "\"  tree=" << w.treeCost << " char=" << w.charCost << "\n";
    cout << "  tree seq: " << w.treeSeq << "\n  char seq: " << w.charSeq << "\n";
    cout << "  TREE "; TreeDiff::formatDiffs(cout, TreeDiff::calculate(w.initial, w.goal, config), w.initial);
    auto charPlans = CharDiff::calculate(w.initial, w.goal, config);
    cout << "  CHAR "; TreeDiff::formatDiffs(cout, charPlans.empty() ? vector<DiffState>{} : charPlans.front().diffs, w.initial);
  }
}
