#include "CharDiff.h"

#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "Tree.h"

using namespace std;
using TreeDiff::childLevel;
using TreeDiff::deleteCost;
using TreeDiff::Level;
using TreeDiff::levelCost;
using TreeDiff::Tree;

namespace CharDiff {
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

// Delete-cost oracle: price deleting a flat span [from,to) as the cheapest set
// of Vim delete commands that *tile* it — a min over a small motion menu, not a
// rigid top-down whole-unit walk. From any position p the candidate edges are:
//   - char run `x`     : p -> p+1, deleteCost(Char)=1 per char (so k chars cost k)
//   - `dw`/`de`        : p -> next Word start/end,    deleteCost(Word)=2  (valid mid-word)
//   - `dW`/`dE`        : p -> next BigWord start/end,  deleteCost(BigWord)=3
//   - `{n}dd`/`{n}dap` : line/paragraph start -> any later start, flat
//                        deleteCost(Line)=2 / deleteCost(Paragraph)=3 (count-independent)
// The min tiling keeps whole-unit deletes cheap (a line = one `dd`) while pricing
// partial/cross-word spans honestly: deleting "bc xy" tiles as `dw` + `2x` ~ 4,
// not the old whole-units-only fall-through to a flat char run = 1. Mid-word
// `dw`/`de` are first-class edges, which is what fixes that under-pricing.
//
// PENALTY ADJUSTMENT (deferred): a `{count}` command carries mental overhead
// beyond its keystrokes (working out "how many?"). When added it attaches PER
// COMMAND — i.e. on the edge cost as f(count) — so the shortest path stays
// additive; a per-span-total-commands surcharge would not be edge-decomposable
// and would need an extra DP dimension. First pass omits it: pricing a char run
// at 1/char already biases the tiling toward word motions over a long `{k}x`.
//
// CharDiff's buffer is fixed during search, so the full delCost[from][to] table
// is precomputed once (O(N^2): one forward DAG-shortest-path per start over an
// O(1)-out-degree motion graph) and read O(1) by the G/D/F search.
class DeleteCost {
public:
  DeleteCost(const Tree& tree, double scale) : n_(static_cast<int>(tree.text.size())) {
    buildBoundaries(tree);
    table_.assign(n_ + 1, vector<double>(n_ + 1, INF));
    for (int a = 0; a <= n_; a++) sweepFrom(a, scale);
  }

  double cost(int from, int to) const { return from >= to ? 0.0 : table_[from][to]; }

private:
  int n_;
  vector<int> nextWordStart_, nextWordEnd_, nextBigStart_, nextBigEnd_;
  vector<char> isLineStart_, isParaStart_;
  vector<int> lineStarts_, paraStarts_;  // sorted, terminated by n_
  vector<vector<double>> table_;

  // Smallest position q > p flagged in `mark` (n_+1 if none).
  vector<int> nextAfter(const vector<char>& mark) const {
    vector<int> nxt(n_ + 1, n_ + 1);
    for (int p = n_ - 1; p >= 0; p--) nxt[p] = mark[p + 1] ? p + 1 : nxt[p + 1];
    return nxt;
  }

  void buildBoundaries(const Tree& tree) {
    vector<char> wStart(n_ + 1, 0), wEnd(n_ + 1, 0), bStart(n_ + 1, 0), bEnd(n_ + 1, 0);
    // Tree word spans run [start, nextStart), so text.end includes trailing
    // whitespace and coincides with the next word's start (the `dw` target). The
    // `de`/`dE` target is the word's core end — last non-blank + 1, before that
    // whitespace. Mark that instead so `de` exists as a distinct, cheaper edge for
    // deletions that stop at the word (e.g. "quick" without its trailing space).
    auto coreEnd = [&](const Node& nd) {
      int e = nd.text.end;
      while (e > nd.text.begin && (tree.text[e - 1] == ' ' || tree.text[e - 1] == '\t')) e--;
      return e;
    };
    for (const Node& nd : tree[Level::Word]) { wStart[nd.text.begin] = 1; wEnd[coreEnd(nd)] = 1; }
    for (const Node& nd : tree[Level::BigWord]) { bStart[nd.text.begin] = 1; bEnd[coreEnd(nd)] = 1; }
    nextWordStart_ = nextAfter(wStart);
    nextWordEnd_ = nextAfter(wEnd);
    nextBigStart_ = nextAfter(bStart);
    nextBigEnd_ = nextAfter(bEnd);

    isLineStart_.assign(n_ + 1, 0);
    isParaStart_.assign(n_ + 1, 0);
    for (const Node& nd : tree[Level::Line]) { lineStarts_.push_back(nd.text.begin); isLineStart_[nd.text.begin] = 1; }
    for (const Node& nd : tree[Level::Paragraph]) { paraStarts_.push_back(nd.text.begin); isParaStart_[nd.text.begin] = 1; }
    lineStarts_.push_back(n_);
    paraStarts_.push_back(n_);
    sort(lineStarts_.begin(), lineStarts_.end());
    sort(paraStarts_.begin(), paraStarts_.end());
  }

  void sweepFrom(int a, double scale) {
    vector<double>& cost = table_[a];
    cost[a] = 0.0;
    for (int p = a; p < n_; p++) {
      if (cost[p] >= INF) continue;
      const double c = cost[p];
      auto relax = [&](int q, double w) {
        if (q > p && q <= n_ && c + w < cost[q]) cost[q] = c + w;
      };
      relax(p + 1, deleteCost(Level::Char) * scale);
      relax(nextWordStart_[p], deleteCost(Level::Word) * scale);
      relax(nextWordEnd_[p], deleteCost(Level::Word) * scale);
      relax(nextBigStart_[p], deleteCost(Level::BigWord) * scale);
      relax(nextBigEnd_[p], deleteCost(Level::BigWord) * scale);
      if (isLineStart_[p])
        for (int s : lineStarts_) relax(s, deleteCost(Level::Line) * scale);
      if (isParaStart_[p])
        for (int s : paraStarts_) relax(s, deleteCost(Level::Paragraph) * scale);
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

}  // namespace CharDiff
