// tests/Debug/VimDiffCostCorpus.cpp
//
// Deterministic corpus of VimDiff plan costs + span signatures, for A/B-ing a
// solver refactor: run before and after, diff the output. Covers collapse on and
// off, K-best, small-alphabet adversarial cases, mutated cases, and the
// benchmark catalog shapes.
//
// Run: VIMFY_SEED_MODE=fixed ./build/tests/vimfy_debug --gtest_filter="VimDiffCostCorpus.*" > corpus.txt
// (fixed seeds keep the Catalog section reproducible; SeedManager defaults to random).

#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Types/Lines.h"
#include "Utils/OptimizerCaseCatalog.h"

using namespace std;

namespace {

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

void dump(const string& tag, const Lines& a, const Lines& b, const Config& cfg, int K) {
  for (bool collapse : {false, true}) {
    VimDiff::CostOptions o;
    o.collapseRuns = collapse;
    o.maxPlans = K;
    auto plans = VimDiff::calculate(a, b, cfg, o);
    cout << tag << (collapse ? " C" : " E") << " plans=" << plans.size();
    for (auto& p : plans) {
      cout << " | " << fixed << setprecision(6) << p.cost << ":";
      for (auto& d : p.diffs)
        cout << " " << d.beginPos.line << "," << d.beginPos.col << "[" << d.deletedText.size()
             << ">" << d.insertedText.size() << "]";
    }
    cout << "\n";
  }
}

}  // namespace

TEST(VimDiffCostCorpus, SmallAlphabet) {
  const Config cfg = Config::qwerty();
  mt19937 rng(12345);
  for (int alphabet : {2, 3, 4})
    for (int it = 0; it < 600; it++) {
      Lines a = randomBuffer(rng, 5, 8, alphabet);
      Lines b = randomBuffer(rng, 5, 8, alphabet);
      dump("small" + to_string(alphabet) + "/" + to_string(it), a, b, cfg, 3);
    }
}

TEST(VimDiffCostCorpus, Mutated) {
  const Config cfg = Config::qwerty();
  mt19937 rng(67890);
  uniform_int_distribution<int> ch(0, 5);
  for (int it = 0; it < 1500; it++) {
    Lines a = randomBuffer(rng, 6, 22, 4);
    string g = a.flatten();
    int nmut = 1 + (int)(rng() % 4);
    for (int k = 0; k < nmut; k++) {
      int op = (int)(rng() % 3);
      int pos = (int)(rng() % (g.size() + 1));
      if (op == 0 && pos < (int)g.size()) g[pos] = char('a' + ch(rng));
      else if (op == 1) g.insert(g.begin() + pos, char('a' + ch(rng)));
      else if (pos < (int)g.size()) g.erase(g.begin() + pos);
    }
    dump("mut/" + to_string(it), a, Lines::unflatten(g), cfg, 3);
  }
}

TEST(VimDiffCostCorpus, Catalog) {
  const Config cfg = Config::uniform();
  vector<CompositionCaseSpec> specs = {
      {"BufferSize/10", 10, 20, 5}, {"BufferSize/20", 20, 20, 5}, {"BufferSize/40", 40, 20, 5},
      {"EditCount/2", 15, 20, 2},   {"EditCount/10", 15, 20, 10},
  };
  for (const CompositionCaseSpec& spec : specs)
    for (int seed = 0; seed < DEFAULT_SEED_COUNT; seed++) {
      CompositionSetup s = buildCompositionSetup(spec, seed);
      dump(spec.name + "/" + to_string(seed), s.initial, s.goal, cfg, 2);
    }
}
