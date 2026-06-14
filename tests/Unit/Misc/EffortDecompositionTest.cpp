// Pins the RunningEffort monoid identity that VimDiff's insert-cost collapse
// relies on: because every metric is bigram-window with a one-key boundary,
// the standalone effort of typing any substring s[q:j) equals
// PS(j) - PS(q) - cut(q), where PS is the prefix effort of typing s[0:j) as one
// chained sequence and cut(q) is the single bigram correction straddling q.
// If a future effort metric grows the context window (e.g. a long same-hand-run
// penalty), this test fails and VimDiff's solveAll() must be revisited.

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"

using namespace std;

namespace {

double standaloneEffort(string_view text, const Config& config) {
  KeyedSequence ks;
  ks.append(text);
  return RunningEffort(ks.keys, config).getEffort(config);
}

void checkDecomposition(const string& text, const Config& config) {
  const int m = (int)text.size();
  vector<RunningEffort> seg;
  vector<double> segEffort(m);
  seg.reserve(m);
  for (int t = 0; t < m; t++) {
    KeyedSequence one;
    one.append(string_view(text).substr(t, 1));
    seg.emplace_back(one.keys, config);
    segEffort[t] = seg[t].getEffort(config);
  }
  vector<double> ps(m + 1, 0.0), cut(m + 1, 0.0);
  RunningEffort acc = seg[0];
  ps[1] = segEffort[0];
  for (int t = 1; t < m; t++) ps[t + 1] = acc.appendFrom(seg[t], config);
  for (int q = 1; q < m; q++) {
    cut[q] = RunningEffort::merge(seg[q - 1], seg[q]).getEffort(config) -
             segEffort[q - 1] - segEffort[q];
  }

  for (int q = 0; q < m; q++) {
    for (int j = q + 1; j <= m; j++) {
      const double direct = standaloneEffort(string_view(text).substr(q, j - q), config);
      const double viaPrefix = ps[j] - ps[q] - cut[q];
      ASSERT_NEAR(direct, viaPrefix, 1e-9)
          << "text=\"" << text << "\" q=" << q << " j=" << j;
    }
  }
}

}  // namespace

TEST(EffortDecomposition, PrefixFormulaMatchesStandaloneEffort) {
  const vector<Config> configs = {Config::qwerty(), Config::uniform(), Config::colemakDh()};
  const string alphabet = "abcdefghijklmnopqrstuvwxyzABCXYZ0189 _;:(){}\"'.,\n\t";
  mt19937 rng(20260612);
  uniform_int_distribution<int> lenDist(1, 40);
  uniform_int_distribution<int> charDist(0, (int)alphabet.size() - 1);

  for (const Config& config : configs) {
    checkDecomposition("the quick fox", config);
    checkDecomposition("VimDiff::calc(a, b);", config);
    for (int it = 0; it < 30; it++) {
      string s;
      const int len = lenDist(rng);
      for (int t = 0; t < len; t++) s += alphabet[charDist(rng)];
      checkDecomposition(s, config);
    }
  }
}
