#include "VimDiff.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Tree.h"
#include "VimCore/CharMask.h"

using namespace std;
using DiffTree::childLevel;
using DiffTree::deleteCost;
using DiffTree::Level;
using DiffTree::levelCost;
using DiffTree::Tree;

namespace VimDiff {
namespace {

constexpr double INF = numeric_limits<double>::max() / 4.0;
using TextRange = Tree::TextRange;
using Node = Tree::Node;

struct EditSpan {
  TextRange oldText;
  TextRange newText;
};

// Coarsest-cover traversal cost of the matched gap [from,to): whole tree units
// collapse to one motion at their level (a paragraph = one `}`), edges recurse.
double moveCover(const Tree& tree, int from, int to, Level level, double scale) {
  if (from >= to) return 0.0;
  if (level == Level::Char) return (to - from) * levelCost(Level::Char) * scale;
  double sum = 0.0;
  int pos = from;
  for (const Node& node : tree[level]) {
    if (node.text.begin >= to) break;
    if (node.text.end <= from) continue;
    if (node.text.begin >= from && node.text.end <= to) {
      sum += moveCover(tree, pos, node.text.begin, childLevel(level), scale);
      sum += levelCost(level) * scale;
      pos = node.text.end;
    }
  }
  sum += moveCover(tree, pos, to, childLevel(level), scale);
  return sum;
}

// Delete-cost oracle: price deleting a flat span [from,to) as the cheapest tiling
// of delete commands, via a per-end-position DP run once per start `a`. For end q,
// the chunk ending there is the cheapest `{k}` delete across levels:
//   - char `{k}x`           : [q-k, q),                          base deleteCost(Char)=1
//   - word `{k}de`/`dw`     : k alnum/_ runs ending at q,        base deleteCost(Word)=2
//   - bigword `{k}dE`/`dW`  : k non-blank runs ending at q,      base deleteCost(BigWord)=3
//   - line/para `{k}dd`/`dap`: k whole lines/paragraphs ending at q, base 2 / 3
// plus the count penalty `penalty(k) = digits(k) + sqrt(k) - 1` (0 at k=1, so a plain
// uncounted command is just `base`; concave, so each extra unit costs less).
//
// Word/big runs are read from character class and are SPAN-LOCAL: a run start is
// clamped to `a`, so a contiguous run counts as one word even inside a larger global
// word (deleting "bbbbbbb" out of "abbbbbbba" = one `de` = 2, not 7 chars). A chunk
// may swallow whitespace on one side — `dw` trailing, or `de` from a leading space —
// never both; interior separators inside a counted multi-run chunk are necessarily
// included. Newlines are neither word/big/ws, so word/big runs never cross lines.
// The clamp also lets a partial-word delete (a run prefix) cost `base`; that
// over-credits vs a real `de`, but keeps the partitioner from over-pricing mid-word
// cuts (single chars still cost 1, the char edge winning).
//
// Words/bigwords/chars cap the count scan at CAP (there are many); lines/paragraphs
// scan all earlier starts (there are few). The buffer is fixed during search, so the
// full table is precomputed once — O(CAP*N^2), plus O(#lines^2) per start — read O(1).
class DeleteCost {
public:
  DeleteCost(const Tree& tree, double scale) : n_(static_cast<int>(tree.text.size())) {
    buildBoundaries(tree);
    buildPenalty(scale);
    table_.assign(n_ + 1, vector<double>(n_ + 1, INF));
    for (int a = 0; a <= n_; a++) sweepFrom(a, scale);
  }

  double cost(int from, int to) const { return from >= to ? 0.0 : table_[from][to]; }

private:
  static constexpr int CAP = 9;  // max count scanned for char/word/bigword

  int n_;
  vector<char> isWord_, isBig_, isWs_;
  vector<int> wsRunStart_;               // ws position -> start of its whitespace run
  vector<int> wordStarts_, wordEnds_;    // ordered alnum/_ runs
  vector<int> bigStarts_, bigEnds_;      // ordered non-blank runs
  vector<int> wordIdx_, bigIdx_;         // word/big char -> index into the run lists
  vector<int> lineStarts_, paraStarts_;  // sorted, terminated by n_
  vector<int> lineIdx_, paraIdx_;        // line/para start position -> list index, else -1
  vector<double> penalty_;               // (digits(k)+sqrt(k)-1)*scale, 0 at k<=1
  vector<vector<double>> table_;

  void buildBoundaries(const Tree& tree) {
    const string& text = tree.text;
    isWord_.assign(n_, 0);
    isBig_.assign(n_, 0);
    isWs_.assign(n_, 0);
    wsRunStart_.assign(n_, 0);
    wordIdx_.assign(n_, -1);
    bigIdx_.assign(n_, -1);
    for (int p = 0; p < n_; p++) {
      isWord_[p] = VimCore::CharMask::isSmallWord(text[p]);
      isBig_[p] = VimCore::CharMask::isBigWord(text[p]);
      isWs_[p] = VimCore::CharMask::isWhitespace(text[p]);
    }
    for (int p = 0; p < n_; p++) {
      wsRunStart_[p] = (isWs_[p] && p > 0 && isWs_[p - 1]) ? wsRunStart_[p - 1] : p;
      if (isWord_[p]) {
        if (p == 0 || !isWord_[p - 1]) wordStarts_.push_back(p);
        wordIdx_[p] = (int)wordStarts_.size() - 1;
        if (p + 1 == n_ || !isWord_[p + 1]) wordEnds_.push_back(p + 1);
      }
      if (isBig_[p]) {
        if (p == 0 || !isBig_[p - 1]) bigStarts_.push_back(p);
        bigIdx_[p] = (int)bigStarts_.size() - 1;
        if (p + 1 == n_ || !isBig_[p + 1]) bigEnds_.push_back(p + 1);
      }
    }
    lineIdx_.assign(n_ + 1, -1);
    paraIdx_.assign(n_ + 1, -1);
    for (const Node& nd : tree[Level::Line]) lineStarts_.push_back(nd.text.begin);
    for (const Node& nd : tree[Level::Paragraph]) paraStarts_.push_back(nd.text.begin);
    lineStarts_.push_back(n_);
    paraStarts_.push_back(n_);
    sort(lineStarts_.begin(), lineStarts_.end());
    sort(paraStarts_.begin(), paraStarts_.end());
    for (int i = 0; i < (int)lineStarts_.size(); i++) lineIdx_[lineStarts_[i]] = i;
    for (int i = 0; i < (int)paraStarts_.size(); i++) paraIdx_[paraStarts_[i]] = i;
  }

  void buildPenalty(double scale) {
    penalty_.assign(n_ + 2, 0.0);
    for (int k = 2; k <= n_ + 1; k++) {
      const int digits = (int)to_string(k).size();
      penalty_[k] = (digits + sqrt((double)k) - 1.0) * scale;
    }
  }

  // Counted word/bigword deletes ending at q: find the run whose chunk ends at q
  // (q-1 inside/at-end of a run = de/partial, or q-1 trailing whitespace = dw), then
  // scan k=1..CAP runs back, clamping the start to a, with an optional leading space.
  void relaxRuns(double& best, const vector<double>& cost, int a, int q, double base,
                 const vector<char>& isClass, const vector<int>& idx,
                 const vector<int>& starts, const vector<int>& ends) const {
    const int i = q - 1;
    int j;
    if (isClass[i]) {
      j = idx[i];
    } else if (isWs_[i] && wsRunStart_[i] > 0 && isClass[wsRunStart_[i] - 1]) {
      j = idx[wsRunStart_[i] - 1];
    } else {
      return;
    }
    for (int k = 1; k <= CAP && j - k + 1 >= 0; k++) {
      const int run = j - k + 1;
      if (ends[run] <= a) break;  // run fully before the deletion start
      best = min(best, cost[max(a, starts[run])] + base + penalty_[k]);
      const int rs = starts[run];
      if (rs > a && isWs_[rs - 1])  // leading `de` from the space before the run
        best = min(best, cost[max(a, wsRunStart_[rs - 1])] + base + penalty_[k]);
    }
  }

  // Counted whole-line/paragraph deletes ending at q (q must be a start or n_): scan
  // all earlier starts >= a, k = number of units between.
  void relaxUnits(double& best, const vector<double>& cost, int a, int q, double base,
                  const vector<int>& idxAt, const vector<int>& starts) const {
    if (idxAt[q] < 0) return;
    const int jq = idxAt[q];
    for (int i = jq - 1; i >= 0; i--) {
      const int ls = starts[i];
      if (ls < a) break;
      best = min(best, cost[ls] + base + penalty_[jq - i]);
    }
  }

  void sweepFrom(int a, double scale) {
    vector<double>& cost = table_[a];
    cost[a] = 0.0;
    const double charBase = deleteCost(Level::Char) * scale;
    const double wordBase = deleteCost(Level::Word) * scale;
    const double bigBase = deleteCost(Level::BigWord) * scale;
    const double lineBase = deleteCost(Level::Line) * scale;
    const double paraBase = deleteCost(Level::Paragraph) * scale;
    for (int q = a + 1; q <= n_; q++) {
      double best = INF;
      for (int k = 1; k <= CAP && q - k >= a; k++)
        best = min(best, cost[q - k] + charBase + penalty_[k]);
      relaxRuns(best, cost, a, q, wordBase, isWord_, wordIdx_, wordStarts_, wordEnds_);
      relaxRuns(best, cost, a, q, bigBase, isBig_, bigIdx_, bigStarts_, bigEnds_);
      relaxUnits(best, cost, a, q, lineBase, lineIdx_, lineStarts_);
      relaxUnits(best, cost, a, q, paraBase, paraIdx_, paraStarts_);
      cost[q] = best;
    }
  }
};

struct PlanSpans {
  vector<EditSpan> spans;
  double cost = 0.0;
};

// K-best DP solver. Each cell holds its `K` cheapest sub-paths (a Cand list)
// rather than a single optimum; merging predecessor lists + edge cost and
// truncating to K at every cell yields the global top-K (additive nonnegative
// costs). A Cand carries an explicit backpointer, so plans are reconstructed by
// walking pointers — no cost-equality matching, and distinct Cands are distinct
// edit-span sets by construction.
struct Solver {
  // One entry in a cell's K-best list. Fields are interpreted by the owning
  // table:
  //   G: `sel` = matched-run length t back to the feeding F cell (-1 = leading-
  //      run base, no predecessor); `rank` indexes F(p-t,q-t)'s list.
  //   D: `sel` = deletion start a; `rank` indexes G(a,q)'s list.
  //   F: `branch` 0 = delete+insert (pred D(i,sel)), 1 = pure-insert
  //      (pred G(i,sel)); `sel` = the new-text split q; `rank` indexes the pred.
  struct Cand {
    double cost;
    int sel;
    int rank;
    int branch = 0;
  };

  const Tree& oldTree;
  const Tree& newTree;
  const Config& config;
  CostOptions options;
  string_view A;
  string_view B;
  int n;
  int m;
  int K;

  vector<vector<vector<Cand>>> gK, dK, fK;  // [x][y] -> up to K candidates, ascending
  vector<vector<char>> gDone, dDone, fDone;
  vector<vector<double>> insTable;  // insTable[b][j] = insert effort of B[b:j)
  unordered_map<long long, double> moveMemo;
  DeleteCost del;  // precomputed boundary-DP delete oracle over the old buffer

  Solver(const Tree& oldT, const Tree& newT, const Config& cfg, CostOptions opts, int maxPlans)
      : oldTree(oldT), newTree(newT), config(cfg), options(opts),
        A(oldT.text), B(newT.text), n((int)A.size()), m((int)B.size()),
        K(max(1, maxPlans)),
        gK(n + 1, vector<vector<Cand>>(m + 1)),
        dK(n + 1, vector<vector<Cand>>(m + 1)),
        fK(n + 1, vector<vector<Cand>>(m + 1)),
        gDone(n + 1, vector<char>(m + 1, 0)),
        dDone(n + 1, vector<char>(m + 1, 0)),
        fDone(n + 1, vector<char>(m + 1, 0)),
        del(oldT, opts.moveDeleteScale) {
    buildInsTable();
  }

  void buildInsTable() {
    insTable.assign(m + 1, vector<double>(m + 1, 0.0));
    vector<RunningEffort> seg;
    seg.reserve(m);
    for (int k = 0; k < m; k++) {
      KeyedSequence one;
      one.append(B.substr(k, 1));
      seg.emplace_back(one.keys, config);
    }
    for (int b = 0; b < m; b++) {
      RunningEffort acc = seg[b];
      insTable[b][b + 1] = acc.getEffort(config);
      for (int j = b + 2; j <= m; j++) {
        insTable[b][j] = acc.appendFrom(seg[j - 1], config);
      }
    }
  }

  double insCost(int b, int j) const { return b < j ? insTable[b][j] : 0.0; }

  double moveCost(int from, int to) {
    long long key = (long long)from * (n + 1) + to;
    auto it = moveMemo.find(key);
    if (it != moveMemo.end()) return it->second;
    double v = moveCover(oldTree, from, to, Level::Paragraph, options.moveDeleteScale);
    moveMemo.emplace(key, v);
    return v;
  }

  double delCost(int from, int to) { return del.cost(from, to); }

  bool prefixEq(int p) const { return A.substr(0, p) == B.substr(0, p); }

  // Sort candidates ascending by cost (stable: ties keep generation order, so
  // reconstruction is deterministic) and truncate to the K cheapest.
  void keepBest(vector<Cand>& cands) const {
    stable_sort(cands.begin(), cands.end(),
                [](const Cand& a, const Cand& b) { return a.cost < b.cost; });
    if ((int)cands.size() > K) cands.resize(K);
  }

  // Positioned to START an edit at (p,q).
  const vector<Cand>& G(int p, int q) {
    if (gDone[p][q]) return gK[p][q];
    vector<Cand>& out = gK[p][q];
    if (p == q && prefixEq(p)) out.push_back({0.0, -1, -1});  // leading run, free
    for (int t = 1; p - t >= 0 && q - t >= 0 && A[p - t] == B[q - t]; t++) {
      const vector<Cand>& pf = F(p - t, q - t);  // matched gap + prior edit
      double mv = moveCost(p - t, p);
      for (int r = 0; r < (int)pf.size(); r++) out.push_back({pf[r].cost + mv, t, r});
    }
    keepBest(out);
    gDone[p][q] = 1;
    return out;
  }

  // Edit started at some (a,q) with a deletion present (a<i), old ptr now i.
  const vector<Cand>& D(int i, int q) {
    if (dDone[i][q]) return dK[i][q];
    vector<Cand>& out = dK[i][q];
    for (int a = 0; a < i; a++) {
      const vector<Cand>& g = G(a, q);
      double dc = delCost(a, i);
      for (int r = 0; r < (int)g.size(); r++) out.push_back({g[r].cost + dc, a, r});
    }
    keepBest(out);
    dDone[i][q] = 1;
    return out;
  }

  // Just completed an edit ending at (i,j).
  const vector<Cand>& F(int i, int j) {
    if (fDone[i][j]) return fK[i][j];
    vector<Cand>& out = fK[i][j];
    for (int q = 0; q <= j; q++) {  // delete + insert
      const vector<Cand>& d = D(i, q);
      double ic = insCost(q, j) + options.diffOpenPenalty;
      for (int r = 0; r < (int)d.size(); r++) out.push_back({d[r].cost + ic, q, r, 0});
    }
    for (int q = 0; q < j; q++) {  // pure insertion (no deletion)
      const vector<Cand>& g = G(i, q);
      double ic = insCost(q, j) + options.diffOpenPenalty;
      for (int r = 0; r < (int)g.size(); r++) out.push_back({g[r].cost + ic, q, r, 1});
    }
    keepBest(out);
    fDone[i][j] = 1;
    return out;
  }

  // Emit spans for the G-candidate at (p,q) rank r (matched runs are free
  // movement, so G only recurses; it emits no span of its own).
  void walkG(int p, int q, int r, vector<EditSpan>& out) {
    const Cand& c = gK[p][q][r];
    if (c.sel < 0) return;  // leading-run base
    walkF(p - c.sel, q - c.sel, c.rank, out);
  }

  // Emit the last edit of the F-candidate at (i,j) rank r, then recurse.
  void walkF(int i, int j, int r, vector<EditSpan>& out) {
    const Cand& c = fK[i][j][r];
    int q = c.sel;
    if (c.branch == 0) {  // delete + insert
      const Cand& dc = dK[i][q][c.rank];
      int a = dc.sel;
      out.push_back(EditSpan{TextRange{a, i}, TextRange{q, j}});
      walkG(a, q, dc.rank, out);
    } else {  // pure insertion
      out.push_back(EditSpan{TextRange{i, i}, TextRange{q, j}});
      walkG(i, q, c.rank, out);
    }
  }

  // Top-K plans overall: gather every completed-at-(i,j) candidate whose trailing
  // run reaches the end, take the K cheapest, and walk each back to spans.
  vector<PlanSpans> reconstructPlans() {
    struct Top {
      double cost;
      int i, j, r;
    };
    vector<Top> tops;
    for (int i = 0; i <= n; i++)
      for (int j = 0; j <= m; j++)
        if (A.substr(i) == B.substr(j)) {
          const vector<Cand>& cands = F(i, j);
          for (int r = 0; r < (int)cands.size(); r++)
            tops.push_back({cands[r].cost, i, j, r});
        }
    stable_sort(tops.begin(), tops.end(),
                [](const Top& a, const Top& b) { return a.cost < b.cost; });
    if ((int)tops.size() > K) tops.resize(K);

    vector<PlanSpans> plans;
    plans.reserve(tops.size());
    for (const Top& t : tops) {
      vector<EditSpan> spans;
      walkF(t.i, t.j, t.r, spans);
      reverse(spans.begin(), spans.end());
      plans.push_back({std::move(spans), t.cost});
    }
    return plans;
  }
};

DiffState diffFromSpan(const Lines& initialLines,
                       const Tree& initialTree,
                       const Tree& goalTree,
                       const EditSpan& span) {
  string deletedText =
      initialTree.text.substr(span.oldText.begin, span.oldText.end - span.oldText.begin);
  string insertedText =
      goalTree.text.substr(span.newText.begin, span.newText.end - span.newText.begin);

  CursorPos begin = DiffText::flatIndexToPosition(span.oldText.begin, initialTree.text);
  CursorPos end = DiffText::advancePositionByText(begin, deletedText);

  return DiffState(begin, end, std::move(deletedText), std::move(insertedText),
                   TransformBoundary(initialLines, begin, end));
}

}  // namespace

vector<Plan> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  Tree initialTree(initialLines);
  Tree goalTree(goalLines);
  if (initialTree.text == goalTree.text) return {};

  Solver solver(initialTree, goalTree, config, options, options.maxPlans);
  vector<PlanSpans> planSpans = solver.reconstructPlans();

  vector<Plan> plans;
  plans.reserve(planSpans.size());
  for (const PlanSpans& ps : planSpans) {
    Plan plan;
    plan.cost = ps.cost;
    plan.diffs.reserve(ps.spans.size());
    for (const EditSpan& span : ps.spans)
      plan.diffs.push_back(diffFromSpan(initialLines, initialTree, goalTree, span));
    plans.push_back(std::move(plan));
  }
  return plans;
}

vector<CostBreakdown> calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  Tree initialTree(initialLines);
  Tree goalTree(goalLines);
  if (initialTree.text == goalTree.text) return {};

  Solver solver(initialTree, goalTree, config, options, options.maxPlans);
  vector<PlanSpans> planSpans = solver.reconstructPlans();
  const double scale = options.moveDeleteScale;
  DeleteCost delCost(initialTree, scale);

  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(planSpans.size());
  for (const PlanSpans& ps : planSpans) {
    CostBreakdown bd;
    double listed = 0.0;
    int prevOldEnd = -1;
    for (const EditSpan& span : ps.spans) {
      DiffState diff = diffFromSpan(initialLines, initialTree, goalTree, span);
      double del = delCost.cost(span.oldText.begin, span.oldText.end);
      KeyedSequence typed;
      typed.append(string_view(goalTree.text).substr(
          span.newText.begin, span.newText.end - span.newText.begin));
      double ins = RunningEffort(typed.keys, config).getEffort(config);
      double mv = prevOldEnd < 0
                      ? 0.0
                      : moveCover(initialTree, prevOldEnd, span.oldText.begin,
                                 Level::Paragraph, scale);
      prevOldEnd = span.oldText.end;
      listed += options.diffOpenPenalty + del + ins + mv;
      bd.regions.push_back(RegionBreakdown{std::move(diff), del, ins, mv});
    }
    bd.total = listed;
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
