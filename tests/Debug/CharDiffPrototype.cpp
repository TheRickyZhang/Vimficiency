// tests/Debug/CharDiffPrototype.cpp
//
// First-pass prototype for the character-level diff partition DP we sketched:
// demote the tree to a *cost oracle* and run the partition search at character
// granularity, so diffs can cross levels freely (no tree-as-structure).
//
// Objective per the agreed model:
//   PENALTY * #edits + sum delCost(deleted) + sum insCost(inserted)
//                    + sum moveCost(interior kept gaps)
// where insCost is the real effort model and delCost/moveCost are coarsest-cover
// tree approximations. Leading/trailing kept runs are free.
//
// This file validates the inner/outer G/D/F DP against a brute-force enumerator
// (same oracle) and prints the chosen partitions so we can eyeball quality.
//
// Run: ./build/tests/vimfy_debug --gtest_filter="CharDiffProto.*"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <string_view>
#include <tuple>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"
#include "Optimizer/DiffPlanner/Tree.h"
#include "Types/Lines.h"

using namespace std;
using TreeDiff::childLevel;
using TreeDiff::deleteCost;
using TreeDiff::Level;
using TreeDiff::levelCost;
using TreeDiff::Tree;

namespace {

constexpr double INF = numeric_limits<double>::max() / 4.0;
constexpr double PENALTY = 1.0;
using Node = Tree::Node;

// Coarsest-cover cost of traversing/deleting [from,to): whole tree units at this
// level are charged weight(level), edges recurse to finer levels.
template <typename WeightFn>
double coverCost(const Tree& tree, int from, int to, Level level, WeightFn weight) {
  if (from >= to) return 0.0;
  if (level == Level::Char) return (to - from) * weight(Level::Char);
  double sum = 0.0;
  int pos = from;
  for (const Node& node : tree[level]) {
    if (node.text.begin >= to) break;
    if (node.text.end <= from) continue;
    if (node.text.begin >= from && node.text.end <= to) {
      sum += coverCost(tree, pos, node.text.begin, childLevel(level), weight);
      sum += weight(level);
      pos = node.text.end;
    }
  }
  sum += coverCost(tree, pos, to, childLevel(level), weight);
  return sum;
}

double moveCost(const Tree& tree, int from, int to) {
  return coverCost(tree, from, to, Level::Paragraph, levelCost);
}
double delCost(const Tree& tree, int from, int to) {
  return coverCost(tree, from, to, Level::Paragraph, deleteCost);
}

// Option A surrogate: a single separable "mode" per level L — charge whole
// L-units at levelCost(L), both edge residuals as chars. Separable in (from,to),
// so min over the 5 levels is a constant set of affine layers => O(p*n^2). This
// is the cost we'd actually optimize if we took the Gotoh/min-of-affine route.
double coverLevelMove(const Tree& tree, int from, int to, Level level) {
  if (from >= to) return 0.0;
  int count = 0, firstBegin = -1, lastEnd = -1;
  for (const Node& node : tree[level]) {
    if (node.text.begin >= to) break;
    if (node.text.end <= from) continue;
    if (node.text.begin >= from && node.text.end <= to) {
      if (firstBegin < 0) firstBegin = node.text.begin;
      lastEnd = node.text.end;
      count++;
    }
  }
  if (count == 0) return (to - from) * levelCost(Level::Char);
  double lead = (firstBegin - from) * levelCost(Level::Char);
  double trail = (to - lastEnd) * levelCost(Level::Char);
  return count * levelCost(level) + lead + trail;
}
double moveApprox(const Tree& tree, int from, int to) {
  double best = (to - from) * levelCost(Level::Char);  // Char mode
  for (Level L : {Level::Paragraph, Level::Line, Level::BigWord, Level::Word})
    best = min(best, coverLevelMove(tree, from, to, L));
  return best;
}
double insCost(string_view text, const Config& config) {
  KeyedSequence typed;
  typed.append(text);
  return RunningEffort(typed.keys, config).getEffort(config);
}

bool approx(double a, double b) { return fabs(a - b) < 1e-6; }

// ---- Inner/outer DP --------------------------------------------------------
//   G[a][b] : positioned to START an edit at (a,b)
//   D[i][b] : edit started, deleted A[a:i) (new ptr still b)   [a<i required]
//   F[i][j] : just COMPLETED an edit ending at (i,j)
struct CharDiff {
  string A, B;
  const Tree& oldTree;
  const Config& config;
  bool useApprox;  // movement oracle: exact coarsest-cover vs Option A surrogate
  int n, m;
  vector<vector<double>> gMemo, dMemo, fMemo;
  vector<vector<bool>> gDone, dDone, fDone;

  CharDiff(const Tree& oldT, const Tree& newT, const Config& cfg, bool approxMove = false)
      : A(oldT.text), B(newT.text), oldTree(oldT), config(cfg), useApprox(approxMove),
        n((int)A.size()), m((int)B.size()),
        gMemo(n + 1, vector<double>(m + 1, INF)),
        dMemo(n + 1, vector<double>(m + 1, INF)),
        fMemo(n + 1, vector<double>(m + 1, INF)),
        gDone(n + 1, vector<bool>(m + 1, false)),
        dDone(n + 1, vector<bool>(m + 1, false)),
        fDone(n + 1, vector<bool>(m + 1, false)) {}

  unordered_map<long long, double> moveMemo, delMemo;

  double moveOracle(int from, int to) {
    long long key = (long long)from * (n + 1) + to;
    auto it = moveMemo.find(key);
    if (it != moveMemo.end()) return it->second;
    double v = useApprox ? moveApprox(oldTree, from, to) : moveCost(oldTree, from, to);
    moveMemo.emplace(key, v);
    return v;
  }
  double delOracle(int from, int to) {
    long long key = (long long)from * (n + 1) + to;
    auto it = delMemo.find(key);
    if (it != delMemo.end()) return it->second;
    double v = delCost(oldTree, from, to);
    delMemo.emplace(key, v);
    return v;
  }

  string_view a(int x, int y) const { return string_view(A).substr(x, y - x); }
  string_view b(int x, int y) const { return string_view(B).substr(x, y - x); }
  bool eq(int a0, int a1, int b0, int b1) const { return a(a0, a1) == b(b0, b1); }

  double G(int p, int q) {
    if (gDone[p][q]) return gMemo[p][q];
    double best = INF;
    if (p == q && eq(0, p, 0, q)) best = 0.0;  // leading free
    for (int t = 1; p - t >= 0 && q - t >= 0 && A[p - t] == B[q - t]; t++) {
      best = min(best, F(p - t, q - t) + moveOracle(p - t, p));
    }
    gMemo[p][q] = best;
    gDone[p][q] = true;
    return best;
  }

  // deletion present: a in [0,i)
  double Ddel(int i, int q) {
    if (dDone[i][q]) return dMemo[i][q];
    double best = INF;
    for (int aa = 0; aa < i; aa++) {
      best = min(best, G(aa, q) + delOracle(aa, i));
    }
    dMemo[i][q] = best;
    dDone[i][q] = true;
    return best;
  }

  double F(int i, int j) {
    if (fDone[i][j]) return fMemo[i][j];
    double best = INF;
    for (int q = 0; q <= j; q++) {  // deletion present, any insertion
      double base = Ddel(i, q);
      if (base >= INF) continue;
      double ins = (q < j) ? insCost(b(q, j), config) : 0.0;
      best = min(best, base + ins);
    }
    for (int q = 0; q < j; q++) {  // pure insertion (a==i), insertion present
      best = min(best, G(i, q) + insCost(b(q, j), config));
    }
    best += PENALTY;
    fMemo[i][j] = best;
    fDone[i][j] = true;
    return best;
  }

  double solve() {
    if (A == B) return 0.0;
    double best = INF;
    for (int i = 0; i <= n; i++)
      for (int j = 0; j <= m; j++)
        if (eq(i, n, j, m)) best = min(best, F(i, j));
    return best;
  }

  struct EditRec { int a, i, b, j; };

  vector<EditRec> reconstruct() {
    int ei = -1, ej = -1;
    double best = INF;
    for (int i = 0; i <= n; i++)
      for (int j = 0; j <= m; j++)
        if (eq(i, n, j, m) && F(i, j) < best) { best = F(i, j); ei = i; ej = j; }

    vector<EditRec> edits;
    int ci = ei, cj = ej;
    while (ci > 0 || cj > 0) {
      double target = fMemo[ci][cj];
      int fa = -1, fb = -1;
      for (int q = 0; q <= cj && fa < 0; q++) {  // del-present
        double base = Ddel(ci, q);
        if (base >= INF) continue;
        double ins = (q < cj) ? insCost(b(q, cj), config) : 0.0;
        if (!approx(base + ins + PENALTY, target)) continue;
        for (int aa = 0; aa < ci; aa++)
          if (approx(G(aa, q) + delOracle(aa, ci), base)) { fa = aa; fb = q; break; }
      }
      if (fa < 0)
        for (int q = 0; q < cj && fa < 0; q++)  // pure-insert
          if (approx(G(ci, q) + insCost(b(q, cj), config) + PENALTY, target)) { fa = ci; fb = q; }

      edits.push_back({fa, ci, fb, cj});

      if (fa == fb && eq(0, fa, 0, fb) && approx(gMemo[fa][fb], 0.0)) break;  // leading
      int pi = -1, pj = -1;
      for (int t = 1; fa - t >= 0 && fb - t >= 0 && A[fa - t] == B[fb - t]; t++)
        if (approx(F(fa - t, fb - t) + moveOracle(fa - t, fa), gMemo[fa][fb])) {
          pi = fa - t; pj = fb - t; break;
        }
      if (pi < 0) break;
      ci = pi; cj = pj;
    }
    reverse(edits.begin(), edits.end());
    return edits;
  }

  // Score a partition under the EXACT cost model (coarsest-cover movement),
  // regardless of which oracle produced it. Leading/trailing gaps are free.
  double scoreExact(const vector<EditRec>& edits) {
    double total = 0.0;
    int prevOldEnd = -1;
    for (const EditRec& e : edits) {
      total += PENALTY;
      if (e.a < e.i) total += delCost(oldTree, e.a, e.i);
      if (e.b < e.j) total += insCost(b(e.b, e.j), config);
      if (prevOldEnd >= 0) total += moveCost(oldTree, prevOldEnd, e.a);
      prevOldEnd = e.i;
    }
    return total;
  }
};

// ---- Brute force: enumerate every partition under the same oracle ----------
struct Brute {
  string A, B;
  const Tree& oldTree;
  const Config& config;
  int n, m;
  map<tuple<int, int, bool>, double> memo;

  Brute(const Tree& oldT, const Tree& newT, const Config& cfg)
      : A(oldT.text), B(newT.text), oldTree(oldT), config(cfg),
        n((int)A.size()), m((int)B.size()) {}

  string_view bv(int x, int y) const { return string_view(B).substr(x, y - x); }
  bool eq(int a0, int a1, int b0, int b1) const {
    return string_view(A).substr(a0, a1 - a0) == bv(b0, b1);
  }

  double rec(int oi, int oj, bool isStart) {
    auto key = make_tuple(oi, oj, isStart);
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
    double best = eq(oi, n, oj, m) ? 0.0 : INF;  // stop (trailing free)
    int gStart = isStart ? 0 : 1;  // interior gaps must be >=1 (no adjacent edits)
    for (int g = gStart; oi + g <= n && oj + g <= m; g++) {
      if (g > 0 && A[oi + g - 1] != B[oj + g - 1]) break;  // gap must stay matched
      int ea = oi + g, eb = oj + g;
      double moveC = (isStart || g == 0) ? 0.0 : moveCost(oldTree, oi, ea);
      for (int ip = ea; ip <= n; ip++)
        for (int jp = eb; jp <= m; jp++) {
          if (ip == ea && jp == eb) continue;  // empty edit
          double editC = PENALTY + delCost(oldTree, ea, ip) + insCost(bv(eb, jp), config);
          double sub = rec(ip, jp, false);
          if (sub < INF) best = min(best, moveC + editC + sub);
        }
    }
    memo[key] = best;
    return best;
  }

  double solve() { return A == B ? 0.0 : rec(0, 0, true); }
};

string show(string_view s) {
  string out;
  for (char c : s) out += (c == '\n') ? string("\\n") : string(1, c);
  return out;
}

void runCase(const Lines& oldL, const Lines& newL, bool print) {
  Tree oldT(oldL), newT(newL);
  Config config = Config::uniform();
  CharDiff dp(oldT, newT, config);
  Brute bf(oldT, newT, config);
  double dpCost = dp.solve();
  double bfCost = bf.solve();

  if (print) {
    cerr << "\n=== \"" << show(oldT.text) << "\"  ->  \"" << show(newT.text)
         << "\"   dp=" << dpCost << " brute=" << bfCost << "\n";
    auto edits = dp.reconstruct();
    int prevI = 0;
    for (size_t k = 0; k < edits.size(); k++) {
      auto& e = edits[k];
      if (k == 0 && e.a > 0)
        cerr << "  keep(lead) \"" << show(dp.a(0, e.a)) << "\"\n";
      if (k > 0 && e.a > prevI)
        cerr << "  keep(move) \"" << show(dp.a(prevI, e.a))
             << "\"  m=" << moveCost(oldT, prevI, e.a) << "\n";
      double d = e.a < e.i ? delCost(oldT, e.a, e.i) : 0.0;
      double ins = e.b < e.j ? insCost(dp.b(e.b, e.j), config) : 0.0;
      cerr << "  edit       del\"" << show(dp.a(e.a, e.i)) << "\" -> ins\""
           << show(dp.b(e.b, e.j)) << "\"  P=1 d=" << d << " i=" << ins << "\n";
      prevI = e.i;
    }
    if (!edits.empty() && prevI < dp.n)
      cerr << "  keep(tail) \"" << show(dp.a(prevI, dp.n)) << "\"\n";
  }

  EXPECT_NEAR(dpCost, bfCost, 1e-6)
      << "DP disagrees with brute for \"" << show(oldT.text) << "\" -> \""
      << show(newT.text) << "\"";
}

}  // namespace

// Quadrangle (Monge) inequality for minimization:
//   w(p1,p3) + w(p2,p4) <= w(p1,p4) + w(p2,p3)   for p1<=p2<=p3<=p4
// If a range-cost satisfies this, the corresponding DP sweep is SMAWK-able in
// amortized O(1)/cell -> O(N^2) EXACT (no additivity needed). This is the only
// surviving route to O(N^2) after additive-M was dropped. Test it empirically.
template <typename W>
int qiViolations(int L, W w, const char* label) {
  int viol = 0;
  double worst = 0.0;
  for (int p1 = 0; p1 <= L; p1++)
    for (int p2 = p1; p2 <= L; p2++)
      for (int p3 = p2; p3 <= L; p3++)
        for (int p4 = p3; p4 <= L; p4++) {
          double lhs = w(p1, p3) + w(p2, p4);
          double rhs = w(p1, p4) + w(p2, p3);
          if (lhs > rhs + 1e-9) {
            viol++;
            worst = max(worst, lhs - rhs);
            if (viol <= 3)
              cerr << "  QI VIOLATION " << label << " ("
                   << p1 << "," << p2 << "," << p3 << "," << p4
                   << ") lhs=" << lhs << " rhs=" << rhs << "\n";
          }
        }
  cerr << "  QI " << label << ": " << viol << " violations"
       << (viol ? "  worst gap=" + to_string(worst) : string("  (holds)")) << "\n";
  return viol;
}

TEST(CharDiffProto, QuadrangleInequality) {
  Config config = Config::uniform();
  vector<Lines> bufs = {
      {"hello world foo bar"},
      {"the quick brown fox jumps"},
      {"aaa", "bbb", "ccc"},
      {"a b", "c d", "", "e f", "g h"},
      {"int x = foo(bar, baz);"},
  };
  int total = 0;
  for (const Lines& buf : bufs) {
    Tree t(buf);
    int L = (int)t.text.size();
    cerr << "\n--- buffer \"" << show(t.text) << "\" (L=" << L << ")\n";
    total += qiViolations(L, [&](int x, int y) { return moveCost(t, x, y); }, "moveCost");
    total += qiViolations(L, [&](int x, int y) { return delCost(t, x, y); }, "delCost");
    total += qiViolations(L, [&](int x, int y) {
      return insCost(string_view(t.text).substr(x, y - x), config);
    }, "insCost");
  }
  cerr << "\nTOTAL QI violations across all buffers: " << total << "\n";
}

TEST(CharDiffProto, RandomStressVsBrute) {
  Config config = Config::uniform();
  mt19937 rng(12345);
  const string alphabet = "ab c\n";  // tiny alphabet so matches occur
  int mismatches = 0, tested = 0;
  auto gen = [&]() {
    int len = 1 + (int)(rng() % 6);
    string s;
    for (int k = 0; k < len; k++) s += alphabet[rng() % alphabet.size()];
    // avoid leading/trailing newline degeneracies for Lines round-trip
    while (!s.empty() && (s.front() == '\n')) s.front() = 'a';
    while (!s.empty() && (s.back() == '\n')) s.back() = 'b';
    return s;
  };
  auto toLines = [](const string& s) {
    Lines l;
    string cur;
    for (char c : s) {
      if (c == '\n') { l.push_back(cur); cur.clear(); }
      else cur += c;
    }
    l.push_back(cur);
    return l;
  };
  for (int it = 0; it < 4000; it++) {
    Lines o = toLines(gen()), n = toLines(gen());
    Tree ot(o), nt(n);
    if ((int)ot.text.size() > 8 || (int)nt.text.size() > 8) continue;
    tested++;
    double dp = CharDiff(ot, nt, config).solve();
    double bf = Brute(ot, nt, config).solve();
    if (!approx(dp, bf)) {
      mismatches++;
      if (mismatches <= 5)
        cerr << "MISMATCH \"" << show(ot.text) << "\" -> \"" << show(nt.text)
             << "\"  dp=" << dp << " brute=" << bf << "\n";
    }
  }
  cerr << "random stress: " << tested << " cases tested, " << mismatches
       << " mismatches\n";
  EXPECT_EQ(mismatches, 0);
}

// Option A quality: run the DP with the exact movement oracle and with the
// separable per-level surrogate, score BOTH partitions under the exact model,
// and report regret = exactCost(approxPick) - exactCost(exactPick). Regret is
// the true-cost penalty of the partition the O(n^2) surrogate would pick.
TEST(CharDiffProto, OptionAApproxQuality) {
  Config config = Config::uniform();
  mt19937 rng(2024);
  const string alphabet = "ab c\n";
  auto gen = [&]() {
    int len = 1 + (int)(rng() % 9);
    string s;
    for (int k = 0; k < len; k++) s += alphabet[rng() % alphabet.size()];
    while (!s.empty() && s.front() == '\n') s.front() = 'a';
    while (!s.empty() && s.back() == '\n') s.back() = 'b';
    return s;
  };
  auto toLines = [](const string& s) {
    Lines l; string cur;
    for (char c : s) { if (c == '\n') { l.push_back(cur); cur.clear(); } else cur += c; }
    l.push_back(cur);
    return l;
  };
  auto sameParts = [](const vector<CharDiff::EditRec>& x,
                      const vector<CharDiff::EditRec>& y) {
    if (x.size() != y.size()) return false;
    for (size_t k = 0; k < x.size(); k++)
      if (x[k].a != y[k].a || x[k].i != y[k].i || x[k].b != y[k].b || x[k].j != y[k].j)
        return false;
    return true;
  };

  int tested = 0, changed = 0, regretCount = 0;
  double totalRegret = 0.0, maxRegret = 0.0, totalExactOpt = 0.0;
  for (int it = 0; it < 6000; it++) {
    Lines o = toLines(gen()), n = toLines(gen());
    Tree ot(o), nt(n);
    if (ot.text == nt.text) continue;
    tested++;

    CharDiff exact(ot, nt, config, /*approxMove=*/false);
    CharDiff approxd(ot, nt, config, /*approxMove=*/true);
    auto exactPick = exact.reconstruct();
    auto approxPick = approxd.reconstruct();

    double exactOpt = exact.scoreExact(exactPick);
    double approxCost = exact.scoreExact(approxPick);
    double regret = approxCost - exactOpt;

    totalExactOpt += exactOpt;
    if (!sameParts(exactPick, approxPick)) changed++;
    if (regret > 1e-6) { regretCount++; totalRegret += regret; maxRegret = max(maxRegret, regret); }
  }
  cerr << "\n=== Option A (separable per-level movement surrogate) ===\n"
       << "cases:                 " << tested << "\n"
       << "partition changed:     " << changed << " (" << (100.0 * changed / tested) << "%)\n"
       << "nonzero regret:        " << regretCount << " (" << (100.0 * regretCount / tested) << "%)\n"
       << "mean regret (all):     " << (totalRegret / tested) << " keystrokes\n"
       << "mean regret (when >0): " << (regretCount ? totalRegret / regretCount : 0.0) << "\n"
       << "max regret:            " << maxRegret << "\n"
       << "mean exact optimum:    " << (totalExactOpt / tested) << "\n"
       << "regret as % of optimum:" << (100.0 * totalRegret / totalExactOpt) << "%\n";
}

// Option A on LARGER, structured buffers (real words/lines/paragraphs), where
// the edge-as-chars surrogate is most likely to misprice. No brute force — just
// exact-DP vs approx-DP, both scored under the exact model.
TEST(CharDiffProto, OptionAApproxQualityStructured) {
  Config config = Config::uniform();
  mt19937 rng(7);
  auto word = [&]() {
    int len = 2 + (int)(rng() % 4);
    string w;
    for (int k = 0; k < len; k++) w += char('a' + rng() % 6);
    return w;
  };
  auto buffer = [&]() {  // several lines of several words, occasional blank line
    int lines = 3 + (int)(rng() % 5);
    string s;
    for (int l = 0; l < lines; l++) {
      if (l) s += '\n';
      if (rng() % 5 == 0) { s += '\n'; }  // paragraph break
      int words = 1 + (int)(rng() % 5);
      for (int w = 0; w < words; w++) { if (w) s += ' '; s += word(); }
    }
    return s;
  };
  auto mutate = [&](string s) {  // a few char-level edits => realistic shared-structure diff
    int edits = 1 + (int)(rng() % 4);
    for (int e = 0; e < edits && !s.empty(); e++) {
      int op = rng() % 3, p = rng() % s.size();
      if (op == 0) s.insert(s.begin() + p, char('a' + rng() % 6));
      else if (op == 1) s.erase(s.begin() + p);
      else s[p] = char('a' + rng() % 6);
    }
    while (!s.empty() && s.front() == '\n') s.erase(s.begin());
    while (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
  };

  int tested = 0, changed = 0, regretCount = 0;
  double totalRegret = 0.0, maxRegret = 0.0, totalExactOpt = 0.0;
  for (int it = 0; it < 1500; it++) {
    string os = buffer();
    string ns = mutate(os);
    Lines o = Lines::unflatten(os), n = Lines::unflatten(ns);
    Tree ot(o), nt(n);
    if (ot.text == nt.text || (int)ot.text.size() > 120 || (int)nt.text.size() > 120) continue;
    tested++;
    CharDiff exact(ot, nt, config, false), approxd(ot, nt, config, true);
    auto ep = exact.reconstruct(), ap = approxd.reconstruct();
    double eopt = exact.scoreExact(ep), acost = exact.scoreExact(ap);
    double regret = acost - eopt;
    totalExactOpt += eopt;
    if (ep.size() != ap.size() || acost != eopt) {
      bool diff = ep.size() != ap.size();
      for (size_t k = 0; !diff && k < ep.size(); k++)
        diff = ep[k].a != ap[k].a || ep[k].i != ap[k].i || ep[k].b != ap[k].b || ep[k].j != ap[k].j;
      if (diff) changed++;
    }
    if (regret > 1e-6) { regretCount++; totalRegret += regret; maxRegret = max(maxRegret, regret); }
  }
  cerr << "\n=== Option A on structured buffers ===\n"
       << "cases:                 " << tested << "\n"
       << "partition changed:     " << changed << " (" << (100.0 * changed / tested) << "%)\n"
       << "nonzero regret:        " << regretCount << " (" << (100.0 * regretCount / tested) << "%)\n"
       << "mean regret (all):     " << (totalRegret / tested) << " keystrokes\n"
       << "mean regret (when >0): " << (regretCount ? totalRegret / regretCount : 0.0) << "\n"
       << "max regret:            " << maxRegret << "\n"
       << "mean exact optimum:    " << (totalExactOpt / tested) << "\n"
       << "regret as % of optimum:" << (100.0 * totalRegret / totalExactOpt) << "%\n";
}

TEST(CharDiffProto, MatchesBruteAndPrints) {
  struct C { Lines o, n; };
  vector<C> cases = {
      {{"abc"}, {"abc"}},
      {{"abc"}, {"abXc"}},
      {{"hello world"}, {"hello"}},
      {{"foo"}, {"bar"}},
      {{"abcdef"}, {"abXYef"}},
      {{"a b c"}, {"a c"}},
      {{"x + x"}, {"y + y"}},
      {{"ab cd ef"}, {"ab XX ef"}},
      {{"aaa", "bbb"}, {"aaa bbb"}},
  };
  for (auto& c : cases) runCase(c.o, c.n, /*print=*/true);
}
