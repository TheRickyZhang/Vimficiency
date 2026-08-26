// tests/Debug/SparseVsDense.cpp
//
// Feasibility check for the anchor-based sparse planner: production
// VimDiff::calculate (diff-bound, skips matched-run interiors) must return the
// SAME optimum cost as the exact dense solver (VimDiff::calculateBreakdown,
// char-level K-best). The dense solver is O(n^3), so we A/B only on small
// buffers — but small buffers over a tiny alphabet are the adversarial case
// (many coincidental matches => alignment ambiguity), which is exactly where
// jumping over matched runs could miss the optimum if the approach were wrong.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="SparseVsDense.*"

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Types/Lines.h"

using namespace std;

namespace {

// Solver-vs-Solver: collapsed (diff-bound, production default) Plan-1 cost vs the
// exact char-level Plan-1 cost. Both go through calculate, so the cost metric is
// identical — only the coordinate collapse differs.
bool costsAgree(const Lines& a, const Lines& b, const Config& cfg, double& sparseC,
                double& denseC) {
  VimDiff::CostOptions on;
  VimDiff::CostOptions off;
  off.collapseRuns = false;
  auto collapsed = VimDiff::calculate(a, b, cfg, on);
  auto exact = VimDiff::calculate(a, b, cfg, off);
  sparseC = collapsed.empty() ? 0.0 : collapsed.front().cost;
  denseC = exact.empty() ? 0.0 : exact.front().cost;
  return abs(sparseC - denseC) < 1e-6;
}

Lines randomBuffer(mt19937& rng, int maxLines, int maxLen, int alphabet) {
  uniform_int_distribution<int> nl(1, maxLines), ln(0, maxLen), ch(0, alphabet - 1);
  int L = nl(rng);
  vector<string> v;
  for (int i = 0; i < L; i++) {
    string s;
    int k = ln(rng);
    for (int j = 0; j < k; j++) s += char('a' + ch(rng));
    v.push_back(s);
  }
  return Lines(v.begin(), v.end());
}

Lines linesFromFlat(const string& s) {
  vector<string> v;
  string cur;
  for (char c : s) {
    if (c == '\n') {
      v.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  v.push_back(cur);
  return Lines(v.begin(), v.end());
}

}  // namespace

// Two independent small-alphabet buffers: maximal coincidental matching, the
// hardest case for "skip matched-run interiors" to stay exact.
TEST(SparseVsDense, RandomSmallAlphabet) {
  const Config cfg = Config::qwerty();
  mt19937 rng(12345);
  int cases = 0, mismatches = 0;
  double maxDelta = 0;
  for (int alphabet : {2, 3, 4}) {
    for (int it = 0; it < 2500; it++) {
      Lines a = randomBuffer(rng, 5, 8, alphabet);
      Lines b = randomBuffer(rng, 5, 8, alphabet);
      double sc, dc;
      cases++;
      if (!costsAgree(a, b, cfg, sc, dc)) {
        mismatches++;
        maxDelta = max(maxDelta, abs(sc - dc));
        if (mismatches <= 15)
          cout << "MISMATCH a=[" << a.flatten() << "] b=[" << b.flatten()
               << "] sparse=" << sc << " dense=" << dc << "\n";
      }
    }
  }
  cout << "RandomSmallAlphabet: " << cases << " cases, " << mismatches
       << " mismatches, maxDelta=" << maxDelta << "\n";
  EXPECT_EQ(mismatches, 0);
}

// Realistic shape: goal is a few char mutations/insertions/deletions of initial,
// so the buffers share genuine matched runs separated by small changed regions.
TEST(SparseVsDense, RandomMutated) {
  const Config cfg = Config::qwerty();
  mt19937 rng(67890);
  uniform_int_distribution<int> ch(0, 5);
  int cases = 0, mismatches = 0;
  double maxDelta = 0;
  string worstA, worstG;
  double worstSc = 0, worstDc = 0;
  for (int it = 0; it < 4000; it++) {
    Lines a = randomBuffer(rng, 5, 22, 4);
    string g = a.flatten();
    int nmut = 1 + (int)(rng() % 4);
    for (int k = 0; k < nmut; k++) {
      int op = (int)(rng() % 3);
      int pos = (int)(rng() % (g.size() + 1));
      if (op == 0 && pos < (int)g.size()) g[pos] = char('a' + ch(rng));
      else if (op == 1) g.insert(g.begin() + pos, char('a' + ch(rng)));
      else if (pos < (int)g.size()) g.erase(g.begin() + pos);
    }
    double sc, dc;
    cases++;
    if (!costsAgree(a, linesFromFlat(g), cfg, sc, dc)) {
      mismatches++;
      if (abs(sc - dc) > maxDelta) {
        maxDelta = abs(sc - dc);
        worstA = a.flatten();
        worstG = g;
        worstSc = sc;
        worstDc = dc;
      }
    }
  }
  cout << "RandomMutated: " << cases << " cases, " << mismatches
       << " mismatches, maxDelta=" << maxDelta << "\n";
  cout << "WORST collapse=" << worstSc << " exact=" << worstDc << "\n  a=[" << worstA
       << "]\n  g=[" << worstG << "]\n";
  for (bool collapse : {false, true}) {
    VimDiff::CostOptions o;
    o.collapseRuns = collapse;
    auto plans = VimDiff::calculate(linesFromFlat(worstA), linesFromFlat(worstG), cfg, o);
    cout << (collapse ? "  COLLAPSE" : "  EXACT   ")
         << " cost=" << (plans.empty() ? -1 : plans.front().cost) << ":";
    if (!plans.empty())
      for (auto& d : plans.front().diffs)
        cout << " {del=[" << d.deletedText << "] ins=[" << d.insertedText << "]}";
    cout << "\n";
  }
  EXPECT_EQ(mismatches, 0);
}

TEST(SparseVsDense, DumpCase) {
  const Config cfg = Config::qwerty();
  Lines a({"ad", "cdac"}), b({"aad", "cda"});
  for (bool collapse : {false, true}) {
    VimDiff::CostOptions o;
    o.collapseRuns = collapse;
    auto plans = VimDiff::calculate(a, b, cfg, o);
    cout << (collapse ? "COLLAPSE" : "EXACT   ")
         << " cost=" << (plans.empty() ? -1 : plans.front().cost) << "\n";
    if (!plans.empty())
      for (auto& d : plans.front().diffs)
        cout << "  del=[" << d.deletedText << "] ins=[" << d.insertedText << "] at ("
             << d.beginPos.line << "," << d.beginPos.col << ")\n";
  }
}

TEST(SparseVsDense, Curated) {
  const Config cfg = Config::qwerty();
  vector<pair<vector<string>, vector<string>>> cases = {
      {{"foo.bar"}, {"baz.qux"}},
      {{"the quick fox"}, {"the slow fox"}},
      {{"a", "c"}, {"a", "b", "c"}},
      {{"a", "c"}, {"ab", "c"}},
      {{"abab"}, {"abXab"}},
      {{"aaaa"}, {"aaXaa"}},
      {{"xy xy xy"}, {"xy XY xy"}},
      {{"hello world"}, {"hello there world"}},
      {{"one two three"}, {"one 2 three"}},
      {{"", "mid", ""}, {"top", "mid", "bot"}},
      {{"abcabcabc"}, {"abcXbcabc"}},
  };
  int mismatches = 0;
  for (auto& [ai, bi] : cases) {
    Lines a(ai.begin(), ai.end()), b(bi.begin(), bi.end());
    double sc, dc;
    if (!costsAgree(a, b, cfg, sc, dc)) {
      mismatches++;
      cout << "MISMATCH a=[" << a.flatten() << "] b=[" << b.flatten()
           << "] sparse=" << sc << " dense=" << dc << "\n";
    }
  }
  EXPECT_EQ(mismatches, 0);
}
