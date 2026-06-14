#include "VimDiff.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Effort/RunningEffort.h"
#include "Keyboard/KeyedSequence.h"
#include "MyersDiff.h"
#include "Tree.h"
#include "Utils/Debug.h"
#include "VimCore/CharMask.h"

using namespace std;
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

// Counted-command penalty: digits typed + concave mental cost, 0 at k<=1.
// Shared by the tiling oracles and the hard-split crossing estimates.
double countPenalty(int k, double scale) {
  if (k <= 1) return 0.0;
  return ((int)to_string(k).size() + sqrt((double)k) - 1.0) * scale;
}

// Tiled command-cost oracle, shared by deletion and movement: price a flat span
// [from,to) as the cheapest tiling of counted commands, via a per-end-position DP
// run once per start `a`. For end q, the chunk ending there is the cheapest `{k}`
// command across levels. Deletion pays the operator (`deleteCost`); movement is the
// bare motion (`levelCost`) — the same shapes minus the `d`:
//   - char    `{k}x` / `{k}l`          : [q-k, q),                     base 1 / 1
//   - word    `{k}de`,`dw` / `{k}e`,`w`: k alnum/_ runs ending at q,   base 2 / 1
//   - bigword `{k}dE`,`dW` / `{k}E`,`W`: k non-blank runs ending at q, base 3 / 2
//   - line    `{k}dd` / `{k}j`         : k whole lines ending at q,    base 2 / 1
//   - para    `{k}dap` / `{k}}`        : k paragraphs ending at q,     base 3 / 2
// plus the count penalty `penalty(k) = digits(k) + sqrt(k) - 1` (0 at k=1, so a plain
// uncounted command is just `base`; concave, so each extra unit costs less). The
// chunk shapes transfer to movement exactly because a delete's extent IS its
// motion's landing point (`dw` = `d`+`w`); a movement tiling is a path of landings.
// Line/para movement chunks approximate `{k}j` between unit starts — column
// adjustment within a line is below this oracle's fidelity.
//
// Word/big runs are read from character class and are SPAN-LOCAL: a run start is
// clamped to `a`, so a contiguous run counts as one word even inside a larger global
// word (deleting "bbbbbbb" out of "abbbbbbba" = one `de` = 2, not 7 chars). A chunk
// may swallow whitespace on one side at count `k` — `dw` trailing, or `de` from a
// leading space — and on both sides at count `k+1` (`d{k+1}w` from the leading
// space); interior separators inside a counted multi-run chunk are necessarily
// included. Newlines are neither word/big/ws, so word/big runs never cross lines.
// The clamp also lets a partial-word delete (a run prefix) cost `base`; that
// over-credits vs a real `de`, but keeps the partitioner from over-pricing mid-word
// cuts (single chars still cost 1, the char edge winning).
//
// Words/bigwords/chars cap the count scan at CAP (there are many); lines/paragraphs
// scan all earlier starts (there are few). The buffer is fixed during search, so the
// full table is precomputed once — O(CAP*N^2), plus O(#lines^2) per start — read O(1).
class TilingCost {
public:
  enum class Kind { Delete, Move };

  TilingCost(const Tree& tree, double scale, Kind kind)
      : n_(static_cast<int>(tree.text.size())), kind_(kind) {
    buildBoundaries(tree);
    buildPenalty(scale);
    table_.assign(n_ + 1, vector<double>(n_ + 1, INF));
    for (int a = 0; a <= n_; a++) sweepFrom(a, scale);
  }

  double cost(int from, int to) const { return from >= to ? 0.0 : table_[from][to]; }

private:
  static constexpr int CAP = 9;  // max count scanned for char/word/bigword

  int n_;
  Kind kind_;
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
    for (int k = 2; k <= n_ + 1; k++) penalty_[k] = countPenalty(k, scale);
  }

  // Counted word/bigword chunks ending at q: find the run whose chunk ends at q
  // (q-1 inside/at-end of a run = de/partial, or q-1 trailing whitespace = dw), then
  // scan k=1..CAP runs back, clamping the start to a, with an optional leading space.
  // Starting from that leading space costs one extra count when the chunk also ends
  // in trailing whitespace (`d{k+1}w`), but not in the de-shape (`d{k}e` from the
  // space lands on the k-th run end).
  void relaxRuns(double& best, const vector<double>& cost, int a, int q, double base,
                 const vector<char>& isClass, const vector<int>& idx,
                 const vector<int>& starts, const vector<int>& ends) const {
    const int i = q - 1;
    int j;
    bool endsInWs = false;
    if (isClass[i]) {
      j = idx[i];
    } else if (isWs_[i] && wsRunStart_[i] > 0 && isClass[wsRunStart_[i] - 1]) {
      j = idx[wsRunStart_[i] - 1];
      endsInWs = true;
    } else {
      return;
    }
    for (int k = 1; k <= CAP && j - k + 1 >= 0; k++) {
      const int run = j - k + 1;
      if (ends[run] <= a) break;  // run fully before the chunk start
      best = min(best, cost[max(a, starts[run])] + base + penalty_[k]);
      const int rs = starts[run];
      if (rs > a && isWs_[rs - 1])  // from the space before the run
        best = min(best, cost[max(a, wsRunStart_[rs - 1])] + base + penalty_[k + endsInWs]);
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

  double unitBase(Level level) const {
    return kind_ == Kind::Delete ? deleteCost(level) : levelCost(level);
  }

  void sweepFrom(int a, double scale) {
    vector<double>& cost = table_[a];
    cost[a] = 0.0;
    const double charBase = unitBase(Level::Char) * scale;
    const double wordBase = unitBase(Level::Word) * scale;
    const double bigBase = unitBase(Level::BigWord) * scale;
    const double lineBase = unitBase(Level::Line) * scale;
    const double paraBase = unitBase(Level::Paragraph) * scale;
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
  vector<vector<char>> gDone, dDone;
  // RunningEffort is a monoid with one-key boundary context, so typing effort
  // decomposes exactly: insCost(q,j) = PS_[j] - PS_[q] - cut_[q], where PS_ is
  // the prefix effort of typing B[0:j) as one sequence and cut_[q] is the single
  // bigram correction straddling position q. O(m) storage instead of the old
  // O(m^2) insTable, and it makes F's insertion-split min collapsible.
  vector<double> PS_, cut_;
  TilingCost del;   // precomputed counted-tiling delete oracle over the old buffer
  TilingCost move;  // same tiling with bare-motion bases (dw->w, dd->j, x->l)

  // chargeLeading/chargeTrailing: when this solver covers an interior segment
  // of a hard-split, its leading/trailing kept runs are part of a real
  // inter-edit gap in the full buffer, so movement over them is charged
  // (instead of the whole-problem leading/trailing free rule).
  bool chargeLeading;
  bool chargeTrailing;

  Solver(const Tree& oldT, const Tree& newT, const Config& cfg, CostOptions opts, int maxPlans,
         bool chargeLeadingIn = false, bool chargeTrailingIn = false)
      : oldTree(oldT), newTree(newT), config(cfg), options(opts),
        A(oldT.text), B(newT.text), n((int)A.size()), m((int)B.size()),
        K(max(1, maxPlans)),
        chargeLeading(chargeLeadingIn), chargeTrailing(chargeTrailingIn),
        gK(n + 1, vector<vector<Cand>>(m + 1)),
        dK(n + 1, vector<vector<Cand>>(m + 1)),
        fK(n + 1, vector<vector<Cand>>(m + 1)),
        gDone(n + 1, vector<char>(m + 1, 0)),
        dDone(n + 1, vector<char>(m + 1, 0)),
        del(oldT, opts.moveDeleteScale, TilingCost::Kind::Delete),
        move(oldT, opts.moveDeleteScale, TilingCost::Kind::Move) {
    buildInsPrefix();
  }

  void buildInsPrefix() {
    PS_.assign(m + 1, 0.0);
    cut_.assign(m + 1, 0.0);
    if (m == 0) return;
    vector<RunningEffort> seg;
    vector<double> segEffort(m);
    seg.reserve(m);
    for (int t = 0; t < m; t++) {
      KeyedSequence one;
      one.append(B.substr(t, 1));
      seg.emplace_back(one.keys, config);
      segEffort[t] = seg[t].getEffort(config);
    }
    RunningEffort acc = seg[0];
    PS_[1] = segEffort[0];
    for (int t = 1; t < m; t++) PS_[t + 1] = acc.appendFrom(seg[t], config);
    for (int q = 1; q < m; q++) {
      cut_[q] = RunningEffort::merge(seg[q - 1], seg[q]).getEffort(config) -
                segEffort[q - 1] - segEffort[q];
    }
  }

  double moveCost(int from, int to) const { return move.cost(from, to); }

  double delCost(int from, int to) const { return del.cost(from, to); }

  bool prefixEq(int p) const { return A.substr(0, p) == B.substr(0, p); }

  // Bounded insert into a cost-ascending list capped at K. `upper_bound` keeps
  // equal-cost entries in generation order, matching the previous
  // materialize-then-stable_sort semantics, but at O(K) per candidate — the
  // "O(maxPlans) over the single-plan search" the K-best design promises.
  // Materializing all predecessors per cell and sorting cost O(N log N) per
  // cell (O(N^3 log N) total) and dominated the planner's runtime even at K=1.
  void insertCand(vector<Cand>& cands, Cand c) const {
    if ((int)cands.size() == K && c.cost >= cands.back().cost) return;
    auto it = upper_bound(cands.begin(), cands.end(), c,
                          [](const Cand& a, const Cand& b) { return a.cost < b.cost; });
    cands.insert(it, c);
    if ((int)cands.size() > K) cands.pop_back();
  }

  // Positioned to START an edit at (p,q). Reads fK directly: solveAll() fills
  // F row by row, and G(p,q) only needs F from strictly earlier rows.
  const vector<Cand>& G(int p, int q) {
    if (gDone[p][q]) return gK[p][q];
    vector<Cand>& out = gK[p][q];
    if (p == q && prefixEq(p))  // leading run: free, or charged on interior segments
      out.push_back({chargeLeading ? moveCost(0, p) : 0.0, -1, -1});
    for (int t = 1; p - t >= 0 && q - t >= 0 && A[p - t] == B[q - t]; t++) {
      const vector<Cand>& pf = fK[p - t][q - t];  // matched gap + prior edit
      double mv = moveCost(p - t, p);
      for (int r = 0; r < (int)pf.size(); r++) insertCand(out, {pf[r].cost + mv, t, r});
    }
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
      for (int r = 0; r < (int)g.size(); r++) insertCand(out, {g[r].cost + dc, a, r});
    }
    dDone[i][q] = 1;
    return out;
  }

  // Fill F row by row. Because insCost(q,j) = PS_[j] - PS_[q] - cut_[q], a
  // candidate's cost at any later j is its q-local part plus a j-constant, so
  // each row keeps two running K-best lists of q-adjusted candidates (branch 0
  // over D(i,q), q<=j; branch 1 over G(i,q), q<j) instead of re-scanning all q
  // per cell — O(K) per cell, exact. Materialization inserts all of b0 before
  // b1, matching the old generation order (all delete+insert candidates in
  // ascending q, then all pure-insert) so exact ties break identically.
  void solveAll() {
    for (int i = 0; i <= n; i++) {
      vector<Cand> b0, b1;
      for (int j = 0; j <= m; j++) {
        const vector<Cand>& d = D(i, j);
        for (int r = 0; r < (int)d.size(); r++)
          insertCand(b0, {d[r].cost - PS_[j] - cut_[j], j, r, 0});
        if (j >= 1) {
          const vector<Cand>& g = G(i, j - 1);
          for (int r = 0; r < (int)g.size(); r++)
            insertCand(b1, {g[r].cost - PS_[j - 1] - cut_[j - 1], j - 1, r, 1});
        }
        vector<Cand>& out = fK[i][j];
        const double shift = PS_[j] + options.diffOpenPenalty;
        for (const Cand& c : b0) insertCand(out, {c.cost + shift, c.sel, c.rank, 0});
        for (const Cand& c : b1) insertCand(out, {c.cost + shift, c.sel, c.rank, 1});
      }
    }
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

  // An identical-replace region (deleted text == inserted text) is strictly
  // dominated by merging that content into a neighboring edit (one fewer
  // penalty; del is subadditive), so it can never appear in the optimum — but
  // the K-best enumeration still reaches such partitions as "distinct"
  // alternatives. They are noise for any top-K consumer, and the transform
  // layer rejects identity edits outright, so filter them here.
  bool isDegenerate(const vector<EditSpan>& spans) const {
    for (const EditSpan& s : spans) {
      if (A.substr(s.oldText.begin, s.oldText.end - s.oldText.begin) ==
          B.substr(s.newText.begin, s.newText.end - s.newText.begin))
        return true;
    }
    return false;
  }

  // Top-K plans overall: gather every completed-at-(i,j) candidate whose trailing
  // run reaches the end, then walk them in cost order, keeping the K cheapest
  // non-degenerate plans. (The optimum is always clean, so plan 1 is exact; later
  // slots are best-effort once the per-cell K-best lists interleave with filtered
  // degenerates.)
  vector<PlanSpans> reconstructPlans() {
    solveAll();
    struct Top {
      double cost;
      int i, j, r;
    };
    vector<Top> tops;
    for (int i = 0; i <= n; i++)
      for (int j = 0; j <= m; j++)
        if (A.substr(i) == B.substr(j)) {
          const vector<Cand>& cands = fK[i][j];
          const double tail = chargeTrailing ? moveCost(i, n) : 0.0;
          for (int r = 0; r < (int)cands.size(); r++)
            tops.push_back({cands[r].cost + tail, i, j, r});
        }
    stable_sort(tops.begin(), tops.end(),
                [](const Top& a, const Top& b) { return a.cost < b.cost; });

    vector<PlanSpans> plans;
    plans.reserve(K);
    for (const Top& t : tops) {
      if ((int)plans.size() == K) break;
      vector<EditSpan> spans;
      walkF(t.i, t.j, t.r, spans);
      reverse(spans.begin(), spans.end());
      if (isDegenerate(spans)) continue;
      plans.push_back({std::move(spans), t.cost});
    }
    return plans;
  }
};

// Hard absurdity bound, not a perf envelope: the K-best tables are O(n*m) cells
// and the tiling oracles O(n^2); past ~1e8 cells the upfront allocation alone is
// gigabytes (and the O(N^3) DP would take far longer than any session tolerates).
// Hard-split decomposition is the planned scaling fix; until then, fail loudly
// rather than degrade.
constexpr long long MAX_PLANNER_CELLS = 100'000'000;

void checkPlannerSize(const Tree& initialTree, const Tree& goalTree) {
  const long long n = (long long)initialTree.text.size() + 1;
  const long long m = (long long)goalTree.text.size() + 1;
  CHECK(max(n * m, n * n) <= MAX_PLANNER_CELLS,
        "VimDiff input too large for the planner DP");
}

// =============================================================================
// Hard-split decomposition
// =============================================================================
// Make planner work proportional to changed neighborhoods, not the slice: cut
// at long kept whole-line blocks that no optimal plan straddles, solve the
// segments between them independently, and recombine. A cut is taken only when
// keeping the block provably dominates retyping it:
//
//   P + moveUB(R) + SPLIT_SLACK < ins(R)        (ins(R) <= del(R) + ins(R))
//
// where moveUB is a valid movement tiling of the block ({k}l / {k}j / {k}}).
// SPLIT_SLACK bounds what cutting can lose: chunk concavity coupling across
// each seam (merging two adjacent counted chunks into one saves at most
// base + penalty(CAP) per seam, two seams) plus the closed-form crossing-move
// approximation. Leading/trailing kept blocks are cut unconditionally — they
// are free in the objective and contribute only solver size.
//
// Blocks are whole lines, with the newline that line-aligns them strictly
// inside the matched gap, so both buffers are line-aligned at every cut and
// segment Lines slices have faithful line structure (a segment's first line
// can still fake a paragraph start — bounded oracle wobble, within slack).
// Interior segments charge movement over their leading/trailing kept runs
// (Solver chargeLeading/chargeTrailing); the block itself contributes a
// plan-independent crossing-move constant (ranking-neutral by construction).

constexpr double SPLIT_SLACK = 12.0;
constexpr int SPLIT_MIN_BLOCK = 16;  // fast precheck; the dominance test decides

struct Segment {
  int oldLineBegin, oldLineEnd;  // [begin, end) line ranges
  int newLineBegin, newLineEnd;
  int flatOldBegin, flatNewBegin;
  bool chargeLeading, chargeTrailing;
};

struct Segmentation {
  vector<Segment> segments;
  vector<double> crossingMove;  // per boundary between consecutive segments
};

Segmentation wholeBufferSegmentation(const Lines& initialLines, const Lines& goalLines) {
  Segmentation out;
  out.segments.push_back(Segment{0, (int)initialLines.size(),
                                 0, (int)goalLines.size(),
                                 0, 0, false, false});
  return out;
}

Segmentation computeSegments(const Lines& initialLines, const Lines& goalLines,
                             const Tree& oldTree, const Tree& newTree,
                             const Config& config, const CostOptions& options) {
  const string& A = oldTree.text;
  const int n = (int)A.size();
  const int m = (int)newTree.text.size();
  const double scale = options.moveDeleteScale;
  // No gap can reach the block precheck in a buffer this small ({""} also has
  // no line nodes at all); skip the Myers walk entirely.
  if (n < SPLIT_MIN_BLOCK) return wholeBufferSegmentation(initialLines, goalLines);

  // Old-side line starts (ascending; lines include their trailing newline).
  vector<int> lineStarts;
  for (const Node& nd : oldTree[Level::Line]) lineStarts.push_back(nd.text.begin);
  sort(lineStarts.begin(), lineStarts.end());
  auto lineIndexOf = [&](int flat) {
    return (int)(lower_bound(lineStarts.begin(), lineStarts.end(), flat) - lineStarts.begin());
  };
  vector<int> newLineStarts;
  for (const Node& nd : newTree[Level::Line]) newLineStarts.push_back(nd.text.begin);
  sort(newLineStarts.begin(), newLineStarts.end());
  auto newLineIndexOf = [&](int flat) {
    return (int)(lower_bound(newLineStarts.begin(), newLineStarts.end(), flat) -
                 newLineStarts.begin());
  };
  vector<int> paraStarts;
  for (const Node& nd : oldTree[Level::Paragraph]) paraStarts.push_back(nd.text.begin);
  sort(paraStarts.begin(), paraStarts.end());

  // Matched gaps from the Myers alignment, in flat coordinates on both sides.
  vector<DiffState> myers = MyersDiff::calculate(initialLines, goalLines);
  vector<int> diffBegins;
  struct Gap {
    int oldBegin, newBegin, len;
    bool leading, trailing;
  };
  vector<Gap> gaps;
  int oldFlat = 0, newFlat = 0;
  for (const DiffState& d : myers) {
    // A pure insertion at the buffer end sits one past the last line.
    const int diffOldBegin = d.beginPos.line < (int)lineStarts.size()
                                 ? lineStarts[d.beginPos.line] + d.beginPos.col
                                 : n;
    diffBegins.push_back(diffOldBegin);
    const int gapLen = diffOldBegin - oldFlat;
    if (gapLen > 0) gaps.push_back({oldFlat, newFlat, gapLen, gaps.empty() && oldFlat == 0, false});
    oldFlat = diffOldBegin + (int)d.deletedText.size();
    newFlat = newFlat + gapLen + (int)d.insertedText.size();
  }
  CHECK(n - oldFlat == m - newFlat, "Myers walk out of sync with flat texts");
  if (n - oldFlat > 0) gaps.push_back({oldFlat, newFlat, n - oldFlat, false, true});

  // One maximal whole-line cut block per qualifying gap.
  struct Block {
    int oldBegin, oldEnd, newBegin;
  };
  vector<Block> blocks;
  for (const Gap& g : gaps) {
    if (g.len < SPLIT_MIN_BLOCK) continue;
    // Block bounds: line starts whose aligning newline lies strictly inside
    // the gap (so the new side is provably line-aligned too); position 0 of a
    // leading gap is aligned by construction.
    const int gapEnd = g.oldBegin + g.len;
    int begin = -1;
    if (g.oldBegin == 0 && g.newBegin == 0) {
      begin = 0;
    } else {
      const int li = lineIndexOf(g.oldBegin + 1);
      if (li < (int)lineStarts.size() && lineStarts[li] < gapEnd) begin = lineStarts[li];
    }
    if (begin < 0) continue;
    const int le = lineIndexOf(gapEnd + 1) - 1;  // last line start <= gapEnd
    int end = (le >= 0 && lineStarts[le] > begin) ? lineStarts[le] : -1;
    if (g.trailing && gapEnd == n) end = max(end, n);  // may absorb a final partial line
    if (end <= begin) continue;
    const int len = end - begin;

    if (!g.leading && !g.trailing) {
      // Dominance test: keeping the block must beat any straddle by SLACK.
      KeyedSequence typed;
      typed.append(string_view(A).substr(begin, len));
      const double ins = RunningEffort(typed.keys, config).getEffort(config);
      const int numLines = lineIndexOf(end) - lineIndexOf(begin);
      const double moveUB =
          min(levelCost(Level::Char) * scale + countPenalty(len, scale),
              levelCost(Level::Line) * scale + countPenalty(max(1, numLines), scale));
      if (options.diffOpenPenalty + moveUB + SPLIT_SLACK >= ins) continue;
    }
    blocks.push_back({begin, end, g.newBegin + (begin - g.oldBegin)});
  }
  if (blocks.empty()) return wholeBufferSegmentation(initialLines, goalLines);

  // Crossing-move constant over a block: cheapest closed-form counted motion.
  // Plan-independent, so approximation here is ranking-neutral. The preceding
  // segment's flattened slice drops its last line's newline, so the crossing
  // covers the block plus that one newline (+1 char, +1 line step) — otherwise
  // every cut would underprice the plan by ~1.
  auto crossingCost = [&](const Block& b) {
    const int len = b.oldEnd - b.oldBegin + 1;
    const int numLines = lineIndexOf(b.oldEnd) - lineIndexOf(b.oldBegin) + 1;
    const int numParas =
        (int)(lower_bound(paraStarts.begin(), paraStarts.end(), b.oldEnd) -
              lower_bound(paraStarts.begin(), paraStarts.end(), b.oldBegin));
    double best = levelCost(Level::Char) * scale + countPenalty(len, scale);
    best = min(best, levelCost(Level::Line) * scale + countPenalty(numLines, scale));
    if (numParas > 0)
      best = min(best, levelCost(Level::Paragraph) * scale + countPenalty(numParas + 1, scale));
    return best;
  };

  // Blocks split the buffer into pieces: piece[i] precedes block[i]. Only the
  // first and last piece can be diff-less (interior pieces sit between two
  // blocks from different gaps, with >=1 diff between those gaps); diff-less
  // end pieces are globally free zones and are dropped. The kept pieces form a
  // contiguous range whose internal boundaries map 1:1 onto blocks.
  struct Piece {
    int oldBegin, oldEnd, newBegin, newEnd;
  };
  vector<Piece> pieces;
  int segOld = 0, segNew = 0;
  for (const Block& b : blocks) {
    pieces.push_back({segOld, b.oldBegin, segNew, b.newBegin});
    segOld = b.oldEnd;
    segNew = b.newBegin + (b.oldEnd - b.oldBegin);
  }
  pieces.push_back({segOld, n, segNew, m});

  // A diff begins inside its piece's half-open range, except a pure insertion
  // at the buffer end, which sits exactly at the final piece's end.
  auto diffsWithin = [&](const Piece& p) {
    const int endIncl = p.oldEnd + (p.oldEnd == n ? 1 : 0);
    return (int)(lower_bound(diffBegins.begin(), diffBegins.end(), endIncl) -
                 lower_bound(diffBegins.begin(), diffBegins.end(), p.oldBegin));
  };
  int first = 0, last = (int)pieces.size() - 1;
  while (first <= last && diffsWithin(pieces[first]) == 0) first++;
  while (last >= first && diffsWithin(pieces[last]) == 0) last--;
  CHECK(first <= last, "hard-split lost every diff-bearing piece");
  int covered = 0;
  for (int i = first; i <= last; i++) covered += diffsWithin(pieces[i]);
  CHECK(covered == (int)diffBegins.size(), "hard-split dropped a diff");
  if (first == last && pieces[first].oldEnd - pieces[first].oldBegin == n)
    return wholeBufferSegmentation(initialLines, goalLines);

  Segmentation out;
  for (int i = first; i <= last; i++) {
    const Piece& p = pieces[i];
    // Piece ends at a block begin (a real line start) or the buffer end; the
    // buffer end maps past any zero-length trailing line node so a trailing
    // empty line stays inside the last segment.
    const int oldLineEnd = p.oldEnd == n ? (int)initialLines.size() : lineIndexOf(p.oldEnd);
    const int newLineEnd = p.newEnd == m ? (int)goalLines.size() : newLineIndexOf(p.newEnd);
    out.segments.push_back(Segment{lineIndexOf(p.oldBegin), oldLineEnd,
                                   newLineIndexOf(p.newBegin), newLineEnd,
                                   p.oldBegin, p.newBegin,
                                   /*chargeLeading=*/i > first,
                                   /*chargeTrailing=*/i < last});
    if (i < last) out.crossingMove.push_back(crossingCost(blocks[i]));
  }
  return out;
}

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

// One solved hard-split segment. The solver stays alive so calculateBreakdown
// can read its oracles in segment-local coordinates.
struct SegmentSolve {
  Segment seg;
  std::unique_ptr<Tree> oldTree, newTree;
  std::unique_ptr<Solver> solver;
  vector<PlanSpans> plans;  // segment-local flat coordinates
};

vector<SegmentSolve> solveSegments(const Lines& initialLines, const Lines& goalLines,
                                   const Segmentation& segmentation,
                                   const Config& config, const CostOptions& options) {
  vector<SegmentSolve> out;
  out.reserve(segmentation.segments.size());
  for (const Segment& s : segmentation.segments) {
    SegmentSolve ss;
    ss.seg = s;
    ss.oldTree = std::make_unique<Tree>(
        Lines(initialLines.begin() + s.oldLineBegin, initialLines.begin() + s.oldLineEnd));
    ss.newTree = std::make_unique<Tree>(
        Lines(goalLines.begin() + s.newLineBegin, goalLines.begin() + s.newLineEnd));
    checkPlannerSize(*ss.oldTree, *ss.newTree);
    ss.solver = std::make_unique<Solver>(*ss.oldTree, *ss.newTree, config, options,
                                         options.maxPlans, s.chargeLeading, s.chargeTrailing);
    ss.plans = ss.solver->reconstructPlans();
    CHECK(!ss.plans.empty(), "hard-split segment produced no plans");
    out.push_back(std::move(ss));
  }
  return out;
}

// Cross-sum top-K across segments (each combined plan picks one plan per
// segment; picks are distinct span-sets, so no dedup). Spans are offset to
// full-buffer flat coordinates; crossing-move constants are plan-independent
// and added once at the end.
vector<PlanSpans> combineSegments(const vector<SegmentSolve>& segs,
                                  const vector<double>& crossingMove, int K) {
  auto offsetInto = [](vector<EditSpan>& out, const vector<EditSpan>& spans, const Segment& seg) {
    for (const EditSpan& sp : spans)
      out.push_back(EditSpan{
          TextRange{sp.oldText.begin + seg.flatOldBegin, sp.oldText.end + seg.flatOldBegin},
          TextRange{sp.newText.begin + seg.flatNewBegin, sp.newText.end + seg.flatNewBegin}});
  };

  vector<PlanSpans> combined;
  for (const PlanSpans& p : segs[0].plans) {
    PlanSpans c;
    c.cost = p.cost;
    offsetInto(c.spans, p.spans, segs[0].seg);
    combined.push_back(std::move(c));
  }
  for (size_t s = 1; s < segs.size(); s++) {
    vector<PlanSpans> next;
    next.reserve(combined.size() * segs[s].plans.size());
    for (const PlanSpans& a : combined)
      for (const PlanSpans& b : segs[s].plans) {
        PlanSpans c;
        c.cost = a.cost + b.cost;
        c.spans = a.spans;
        offsetInto(c.spans, b.spans, segs[s].seg);
        next.push_back(std::move(c));
      }
    stable_sort(next.begin(), next.end(),
                [](const PlanSpans& x, const PlanSpans& y) { return x.cost < y.cost; });
    if ((int)next.size() > K) next.resize(K);
    combined = std::move(next);
  }
  double crossTotal = 0.0;
  for (double c : crossingMove) crossTotal += c;
  for (PlanSpans& p : combined) p.cost += crossTotal;
  return combined;
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

  const Segmentation segmentation =
      options.hardSplit
          ? computeSegments(initialLines, goalLines, initialTree, goalTree, config, options)
          : wholeBufferSegmentation(initialLines, goalLines);
  const vector<SegmentSolve> segs =
      solveSegments(initialLines, goalLines, segmentation, config, options);
  vector<PlanSpans> planSpans =
      combineSegments(segs, segmentation.crossingMove, max(1, options.maxPlans));

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

  const Segmentation segmentation =
      options.hardSplit
          ? computeSegments(initialLines, goalLines, initialTree, goalTree, config, options)
          : wholeBufferSegmentation(initialLines, goalLines);
  const vector<SegmentSolve> segs =
      solveSegments(initialLines, goalLines, segmentation, config, options);
  vector<PlanSpans> planSpans =
      combineSegments(segs, segmentation.crossingMove, max(1, options.maxPlans));

  // Owning segment of a full-coordinate old position (regions never straddle a
  // cut block, so the containing segment is unique).
  auto segmentOf = [&](int oldFlat) {
    int si = 0;
    for (size_t s = 0; s < segs.size(); s++)
      if (segs[s].seg.flatOldBegin <= oldFlat) si = (int)s;
    return si;
  };

  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(planSpans.size());
  for (const PlanSpans& ps : planSpans) {
    CostBreakdown bd;
    double listed = 0.0;
    int prevSi = -1, prevLocalEnd = -1;
    for (const EditSpan& span : ps.spans) {
      DiffState diff = diffFromSpan(initialLines, initialTree, goalTree, span);
      const int si = segmentOf(span.oldText.begin);
      const SegmentSolve& seg = segs[si];
      const int localBegin = span.oldText.begin - seg.seg.flatOldBegin;
      const int localEnd = span.oldText.end - seg.seg.flatOldBegin;
      double del = seg.solver->delCost(localBegin, localEnd);
      KeyedSequence typed;
      typed.append(string_view(goalTree.text).substr(
          span.newText.begin, span.newText.end - span.newText.begin));
      double ins = RunningEffort(typed.keys, config).getEffort(config);
      // Inter-region movement, attributed to the later region. Across a cut:
      // previous segment's charged trailing run + the block crossing constant +
      // this segment's charged leading run.
      double mv = 0.0;
      if (prevSi == si) {
        mv = seg.solver->moveCost(prevLocalEnd, localBegin);
      } else if (prevSi >= 0) {
        mv = segs[prevSi].solver->moveCost(prevLocalEnd, segs[prevSi].solver->n);
        for (int b = prevSi; b < si; b++) mv += segmentation.crossingMove[b];
        mv += seg.solver->moveCost(0, localBegin);
      }
      prevSi = si;
      prevLocalEnd = localEnd;
      listed += options.diffOpenPenalty + del + ins + mv;
      bd.regions.push_back(RegionBreakdown{std::move(diff), del, ins, mv});
    }
    bd.total = listed;
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
