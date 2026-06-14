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

// Counted-command penalty: digits typed + concave count cost, 0 at k<=1.
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

  // Builds boundaries + penalty only. query() works immediately; cost() needs
  // buildActiveTable() first.
  TilingCost(const Tree& tree, double scale, Kind kind)
      : n_(static_cast<int>(tree.text.size())), kind_(kind), scale_(scale) {
    buildBoundaries(tree);
    buildPenalty(scale);
  }

  // Cost over ACTIVE unit indices (see buildActiveTable); 0 when from>=to.
  double cost(int from, int to) const { return from >= to ? 0.0 : table_[from][to]; }

  // Build the reduced cost table over active unit boundaries. oldPos are raw
  // boundaries (front 0, back n_); unit u spans [oldPos[u],oldPos[u+1]) and is a
  // forced block transition of cost blockCost[u] when isBlock[u], else tiled with
  // char/word/line/para chunks that never cross a block. Boundaries stay full-
  // context (raw), so there is no slice wobble. O(U^2) over the diff-sized unit
  // count.
  void buildActiveTable(const vector<int>& oldPos, const vector<char>& isBlock,
                        const vector<double>& blockCost) {
    const int U = (int)oldPos.size() - 1;
    table_.assign(U + 1, vector<double>(U + 1, INF));
    if ((int)scratch_.size() < n_ + 1) scratch_.assign(n_ + 1, INF);
    for (int as = 0; as <= U; as++) sweepActive(as, oldPos, isBlock, blockCost);
  }

  // Lazy single tiling query of block-free content [from,to), O(to-from), without
  // the O(N^2) table. Used by the reduction builder for the collapse gate and
  // per-block move/delete costs.
  double query(int from, int to) const {
    if (from >= to) return 0.0;
    if ((int)scratch_.size() < n_ + 1) scratch_.assign(n_ + 1, INF);
    vector<double>& cost = scratch_;
    cost[from] = 0.0;
    const double charBase = unitBase(Level::Char) * scale_;
    const double wordBase = unitBase(Level::Word) * scale_;
    const double bigBase = unitBase(Level::BigWord) * scale_;
    const double lineBase = unitBase(Level::Line) * scale_;
    const double paraBase = unitBase(Level::Paragraph) * scale_;
    for (int q = from + 1; q <= to; q++) {
      double best = INF;
      for (int k = 1; k <= CAP && q - k >= from; k++)
        best = min(best, cost[q - k] + charBase + penalty_[k]);
      relaxRuns(best, cost, from, q, wordBase, isWord_, wordIdx_, wordStarts_, wordEnds_);
      relaxRuns(best, cost, from, q, bigBase, isBig_, bigIdx_, bigStarts_, bigEnds_);
      relaxUnits(best, cost, from, q, lineBase, lineIdx_, lineStarts_);
      relaxUnits(best, cost, from, q, paraBase, paraIdx_, paraStarts_);
      cost[q] = best;
    }
    return cost[to];
  }

private:
  static constexpr int CAP = 9;  // max count scanned for char/word/bigword

  int n_;
  Kind kind_;
  double scale_;
  mutable vector<double> scratch_;  // reused temp for query()
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

  // One row of the active table: tile from active start `as`. Char-stretches use
  // the raw chunk relaxers with the floor reset to the current stretch start (so
  // chunks never cross a block); each block unit is a forced single transition.
  // scratch_ is the raw-indexed working array; only stretch positions and block
  // boundaries are written, and every read is a position written earlier this
  // sweep, so no clearing between sweeps is needed.
  void sweepActive(int as, const vector<int>& oldPos, const vector<char>& isBlock,
                   const vector<double>& blockCost) {
    vector<double>& cost = scratch_;
    vector<double>& row = table_[as];
    const int U = (int)oldPos.size() - 1;
    cost[oldPos[as]] = 0.0;
    row[as] = 0.0;
    int stretchStart = oldPos[as];
    const double charBase = unitBase(Level::Char) * scale_;
    const double wordBase = unitBase(Level::Word) * scale_;
    const double bigBase = unitBase(Level::BigWord) * scale_;
    const double lineBase = unitBase(Level::Line) * scale_;
    const double paraBase = unitBase(Level::Paragraph) * scale_;
    for (int au = as + 1; au <= U; au++) {
      const int rawQ = oldPos[au];
      if (isBlock[au - 1]) {
        cost[rawQ] = cost[oldPos[au - 1]] + blockCost[au - 1];
        stretchStart = rawQ;
      } else {
        double best = INF;
        for (int k = 1; k <= CAP && rawQ - k >= stretchStart; k++)
          best = min(best, cost[rawQ - k] + charBase + penalty_[k]);
        relaxRuns(best, cost, stretchStart, rawQ, wordBase, isWord_, wordIdx_, wordStarts_, wordEnds_);
        relaxRuns(best, cost, stretchStart, rawQ, bigBase, isBig_, bigIdx_, bigStarts_, bigEnds_);
        relaxUnits(best, cost, stretchStart, rawQ, lineBase, lineIdx_, lineStarts_);
        relaxUnits(best, cost, stretchStart, rawQ, paraBase, paraIdx_, paraStarts_);
        cost[rawQ] = best;
      }
      row[au] = cost[rawQ];
    }
  }
};

// FNV-1a over a byte range; O(1) segment-match checks in the reduced DP.
uint64_t fnv1a(string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= (unsigned char)c;
    h *= 1099511628211ull;
  }
  return h;
}

// Reduced coordinate system. Long matched runs collapse to single "block" units;
// changed regions and short matches stay character-level. Each side becomes a
// sequence of units (one block or one char), and both the DP and the tiling oracle
// run over the unit boundaries (oldPos/newPos) so work scales with the diff, not
// the buffer. A block carries its precomputed move/delete cost; new-side typing
// stays raw (insert prefix sums cover collapsed runs directly).
struct Reduction {
  vector<int> oldPos, newPos;                // active raw boundaries; front()=0, back()=n/m
  vector<char> oldIsBlock;                    // per old unit u: [oldPos[u],oldPos[u+1]) is a block
  vector<double> oldBlockMove, oldBlockDel;   // per old unit; 0 for non-blocks
  vector<uint64_t> oldSegHash, newSegHash;    // per unit content hash

  int oldUnits() const { return (int)oldPos.size() - 1; }
  int newUnits() const { return (int)newPos.size() - 1; }
};

// Character-level coordinates: every position is a unit boundary. This is the
// exact baseline; efficiency work must preserve this state space lazily rather
// than pruning matched-run interiors.
Reduction buildReduction(const Tree& oldTree, const Tree& newTree,
                         const Lines&, const Lines&, const Config&, const CostOptions&) {
  const string& A = oldTree.text;
  const string& B = newTree.text;
  const int n = (int)A.size();
  const int m = (int)B.size();
  Reduction r;
  r.oldPos.reserve(n + 1);
  r.newPos.reserve(m + 1);
  for (int i = 0; i <= n; i++) r.oldPos.push_back(i);
  for (int j = 0; j <= m; j++) r.newPos.push_back(j);
  r.oldIsBlock.assign(n, 0);
  r.oldBlockMove.assign(n, 0.0);
  r.oldBlockDel.assign(n, 0.0);
  for (int i = 0; i < n; i++) r.oldSegHash.push_back(fnv1a(string_view(A).substr(i, 1)));
  for (int j = 0; j < m; j++) r.newSegHash.push_back(fnv1a(string_view(B).substr(j, 1)));
  return r;
}

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
  const Reduction& red;
  const vector<int>& oldPos;  // active old index -> raw position
  const vector<int>& newPos;  // active new index -> raw position
  const Config& config;
  CostOptions options;
  string_view A;
  string_view B;
  int n;
  int m;
  int U;  // old unit count; active old indices run 0..U
  int V;  // new unit count; active new indices run 0..V
  int K;
  int commonPrefix_ = 0, commonSuffix_ = 0;  // raw leading/trailing matched lengths

  vector<vector<vector<Cand>>> gK, dK, fK;  // [activeOld][activeNew] -> up to K candidates
  vector<vector<char>> gDone, dDone;
  // RunningEffort is a monoid with one-key boundary context, so typing effort
  // decomposes exactly: insCost(q,j) = PS_[j] - PS_[q] - cut_[q] over RAW new
  // positions, PS_ the prefix effort of typing B[0:j) and cut_[q] the bigram
  // correction straddling q. O(m) storage; a collapsed new run is covered because
  // PS_ spans the whole raw buffer. The DP translates active new index -> raw.
  vector<double> PS_, cut_;
  TilingCost del;   // reduced counted-tiling delete oracle over active old units
  TilingCost move;  // same tiling with bare-motion bases (dw->w, dd->j, x->l)

  Solver(const Tree& oldT, const Tree& newT, const Reduction& reduction, const Config& cfg,
         CostOptions opts, int maxPlans)
      : oldTree(oldT), newTree(newT), red(reduction), oldPos(reduction.oldPos),
        newPos(reduction.newPos), config(cfg), options(opts),
        A(oldT.text), B(newT.text), n((int)A.size()), m((int)B.size()),
        U(reduction.oldUnits()), V(reduction.newUnits()), K(max(1, maxPlans)),
        gK(U + 1, vector<vector<Cand>>(V + 1)),
        dK(U + 1, vector<vector<Cand>>(V + 1)),
        fK(U + 1, vector<vector<Cand>>(V + 1)),
        gDone(U + 1, vector<char>(V + 1, 0)),
        dDone(U + 1, vector<char>(V + 1, 0)),
        del(oldT, opts.moveDeleteScale, TilingCost::Kind::Delete),
        move(oldT, opts.moveDeleteScale, TilingCost::Kind::Move) {
    del.buildActiveTable(red.oldPos, red.oldIsBlock, red.oldBlockDel);
    move.buildActiveTable(red.oldPos, red.oldIsBlock, red.oldBlockMove);
    buildInsPrefix();
    while (commonPrefix_ < n && commonPrefix_ < m && A[commonPrefix_] == B[commonPrefix_])
      commonPrefix_++;
    while (commonSuffix_ < n && commonSuffix_ < m &&
           A[n - 1 - commonSuffix_] == B[m - 1 - commonSuffix_])
      commonSuffix_++;
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

  // Oracle queries take ACTIVE old indices (the reduced tables are active-indexed).
  double moveCost(int from, int to) const { return move.cost(from, to); }
  double delCost(int from, int to) const { return del.cost(from, to); }

  // Old unit i-1 matches new unit j-1: equal length and content (hashed).
  bool unitMatch(int i, int j) const {
    return oldPos[i] - oldPos[i - 1] == newPos[j] - newPos[j - 1] &&
           red.oldSegHash[i - 1] == red.newSegHash[j - 1];
  }

  // Bounded insert into a cost-ascending list capped at K. `upper_bound` keeps
  // equal-cost entries in generation order, at O(K) per candidate.
  void insertCand(vector<Cand>& cands, Cand c) const {
    if ((int)cands.size() == K && c.cost >= cands.back().cost) return;
    auto it = upper_bound(cands.begin(), cands.end(), c,
                          [](const Cand& a, const Cand& b) { return a.cost < b.cost; });
    cands.insert(it, c);
    if ((int)cands.size() > K) cands.pop_back();
  }

  // Positioned to START an edit at active (i,j). A collapsed run is one matched
  // edge, so chaining over active edges treats it as a single free-movement step.
  const vector<Cand>& G(int i, int j) {
    if (gDone[i][j]) return gK[i][j];
    vector<Cand>& out = gK[i][j];
    if (oldPos[i] == newPos[j] && oldPos[i] <= commonPrefix_)
      out.push_back({0.0, -1, -1});  // leading run: free
    for (int t = 1; i - t >= 0 && j - t >= 0 && unitMatch(i - t + 1, j - t + 1); t++) {
      const vector<Cand>& pf = fK[i - t][j - t];  // matched run + prior edit
      double mv = moveCost(i - t, i);
      for (int r = 0; r < (int)pf.size(); r++) insertCand(out, {pf[r].cost + mv, t, r});
    }
    gDone[i][j] = 1;
    return out;
  }

  // Edit started at some active (a,q) with a deletion present (a<i), old ptr now i.
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

  // Fill F row by row. insCost(q,j) = PS_[newPos[j]] - PS_[newPos[q]] - cut_[newPos[q]],
  // so a candidate's cost at any later j is its q-local part plus a j-constant; each
  // row keeps two running K-best lists (branch 0 over D(i,q), q<=j; branch 1 over
  // G(i,q), q<j), O(K) per cell, exact. b0 inserts before b1 to match tie order.
  void solveAll() {
    for (int i = 0; i <= U; i++) {
      vector<Cand> b0, b1;
      for (int j = 0; j <= V; j++) {
        const vector<Cand>& d = D(i, j);
        for (int r = 0; r < (int)d.size(); r++)
          insertCand(b0, {d[r].cost - PS_[newPos[j]] - cut_[newPos[j]], j, r, 0});
        if (j >= 1) {
          const vector<Cand>& g = G(i, j - 1);
          for (int r = 0; r < (int)g.size(); r++)
            insertCand(b1, {g[r].cost - PS_[newPos[j - 1]] - cut_[newPos[j - 1]], j - 1, r, 1});
        }
        vector<Cand>& out = fK[i][j];
        const double shift = PS_[newPos[j]] + options.diffOpenPenalty;
        for (const Cand& c : b0) insertCand(out, {c.cost + shift, c.sel, c.rank, 0});
        for (const Cand& c : b1) insertCand(out, {c.cost + shift, c.sel, c.rank, 1});
      }
    }
  }

  // Emit spans for the G-candidate at active (i,j) rank r (matched runs are free
  // movement, so G only recurses; it emits no span of its own).
  void walkG(int i, int j, int r, vector<EditSpan>& out) {
    const Cand& c = gK[i][j][r];
    if (c.sel < 0) return;  // leading-run base
    walkF(i - c.sel, j - c.sel, c.rank, out);
  }

  // Emit the last edit of the F-candidate at active (i,j) rank r in RAW coordinates,
  // then recurse.
  void walkF(int i, int j, int r, vector<EditSpan>& out) {
    const Cand& c = fK[i][j][r];
    int q = c.sel;
    if (c.branch == 0) {  // delete + insert
      const Cand& dc = dK[i][q][c.rank];
      int a = dc.sel;
      out.push_back(EditSpan{TextRange{oldPos[a], oldPos[i]}, TextRange{newPos[q], newPos[j]}});
      walkG(a, q, dc.rank, out);
    } else {  // pure insertion
      out.push_back(EditSpan{TextRange{oldPos[i], oldPos[i]}, TextRange{newPos[q], newPos[j]}});
      walkG(i, q, c.rank, out);
    }
  }

  // An identical-replace region (deleted text == inserted text) is strictly
  // dominated by merging that content into a neighboring edit, so it never appears
  // in the optimum — but the K-best enumeration still reaches such partitions, and
  // the transform layer rejects identity edits, so filter them here.
  bool isDegenerate(const vector<EditSpan>& spans) const {
    for (const EditSpan& s : spans) {
      if (A.substr(s.oldText.begin, s.oldText.end - s.oldText.begin) ==
          B.substr(s.newText.begin, s.newText.end - s.newText.begin))
        return true;
    }
    return false;
  }

  // Top-K plans overall: gather every completed-at-(i,j) candidate whose trailing
  // run reaches the end (free), then walk them in cost order, keeping the K cheapest
  // non-degenerate plans.
  vector<PlanSpans> reconstructPlans() {
    solveAll();
    struct Top {
      double cost;
      int i, j, r;
    };
    vector<Top> tops;
    for (int i = 0; i <= U; i++)
      for (int j = 0; j <= V; j++)
        if (n - oldPos[i] == m - newPos[j] && n - oldPos[i] <= commonSuffix_) {
          const vector<Cand>& cands = fK[i][j];
          for (int r = 0; r < (int)cands.size(); r++) tops.push_back({cands[r].cost, i, j, r});
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

// Hard absurdity bound, not a perf envelope. Over the reduced unit counts U/V the
// K-best tables are O(U*V) and the oracle O(U*U); long matched runs collapse to
// single units, so this is reached only by a genuinely huge *changed* region.
// Fail loudly rather than degrade.
constexpr long long MAX_PLANNER_CELLS = 100'000'000;

void checkPlannerSize(int U, int V) {
  const long long u = (long long)U + 1;
  const long long v = (long long)V + 1;
  CHECK(max(u * v, u * u) <= MAX_PLANNER_CELLS, "VimDiff diff too large for the planner DP");
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


// Shared pipeline for both public entry points: build the trees, the reduction
// (collapsed/char units), and the solved plans from one unified DP. The trees,
// reduction, and solver are heap-held so the solver's references stay valid when
// the pipeline is returned by value; calculateBreakdown reads the live solver.
struct SolvedPipeline {
  std::unique_ptr<Tree> initialTree, goalTree;
  std::unique_ptr<Reduction> reduction;
  std::unique_ptr<Solver> solver;
  vector<PlanSpans> planSpans;
  bool equal = false;  // initial already equals goal
};

SolvedPipeline runPipeline(const Lines& initialLines, const Lines& goalLines,
                           const Config& config, const CostOptions& options) {
  SolvedPipeline out;
  out.initialTree = std::make_unique<Tree>(initialLines);
  out.goalTree = std::make_unique<Tree>(goalLines);
  if (out.initialTree->text == out.goalTree->text) {
    out.equal = true;
    return out;
  }
  out.reduction = std::make_unique<Reduction>(
      buildReduction(*out.initialTree, *out.goalTree, initialLines, goalLines, config, options));
  checkPlannerSize(out.reduction->oldUnits(), out.reduction->newUnits());
  out.solver = std::make_unique<Solver>(*out.initialTree, *out.goalTree, *out.reduction, config,
                                        options, options.maxPlans);
  out.planSpans = out.solver->reconstructPlans();
  return out;
}

}  // namespace

vector<Plan> calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const SolvedPipeline p = runPipeline(initialLines, goalLines, config, options);
  if (p.equal) return {};

  vector<Plan> plans;
  plans.reserve(p.planSpans.size());
  for (const PlanSpans& ps : p.planSpans) {
    Plan plan;
    plan.cost = ps.cost;
    plan.diffs.reserve(ps.spans.size());
    for (const EditSpan& span : ps.spans)
      plan.diffs.push_back(diffFromSpan(initialLines, *p.initialTree, *p.goalTree, span));
    plans.push_back(std::move(plan));
  }
  return plans;
}

vector<CostBreakdown> calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const SolvedPipeline p = runPipeline(initialLines, goalLines, config, options);
  if (p.equal) return {};
  const Reduction& red = *p.reduction;
  const Solver& solver = *p.solver;
  // Span boundaries are always active positions; map raw old position -> active index.
  auto activeOld = [&](int raw) {
    return (int)(lower_bound(red.oldPos.begin(), red.oldPos.end(), raw) - red.oldPos.begin());
  };

  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(p.planSpans.size());
  for (const PlanSpans& ps : p.planSpans) {
    CostBreakdown bd;
    double listed = 0.0;
    int prevEnd = -1;  // active old index of the previous region's old end
    for (const EditSpan& span : ps.spans) {
      DiffState diff = diffFromSpan(initialLines, *p.initialTree, *p.goalTree, span);
      const int aBegin = activeOld(span.oldText.begin);
      const int aEnd = activeOld(span.oldText.end);
      const double del = solver.delCost(aBegin, aEnd);
      KeyedSequence typed;
      typed.append(string_view(p.goalTree->text).substr(
          span.newText.begin, span.newText.end - span.newText.begin));
      const double ins = RunningEffort(typed.keys, config).getEffort(config);
      // Inter-region movement: one motion from the previous region's end to this
      // region's begin (one unified DP, so no severing and no coupling); the first
      // region is free.
      const double mv = prevEnd < 0 ? 0.0 : solver.moveCost(prevEnd, aBegin);
      prevEnd = aEnd;
      listed += options.diffOpenPenalty + del + ins + mv;
      bd.regions.push_back(RegionBreakdown{std::move(diff), del, ins, mv});
    }
    bd.total = listed;
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
