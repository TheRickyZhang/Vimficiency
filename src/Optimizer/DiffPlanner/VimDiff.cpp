#include "VimDiff.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
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
// Char-level cells kept at each edge of a collapsed matched run, so the DP can slide
// an edit boundary to the cost-optimal alignment (the optimal slide is bounded:
// sliding k chars costs ~k to retype but saves only sub-linear navigation).
constexpr int MATCH_MARGIN = 8;
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

// Span cost oracle for deletion/movement, tiled from counted char/word/bigword/
// line/paragraph commands. Word/bigword chunks are span-local.
class TilingCost {
public:
  enum class Kind { Delete, Move };

  TilingCost(const Tree& tree, double scale, Kind kind)
      : n_(static_cast<int>(tree.text.size())), kind_(kind), scale_(scale) {
    buildBoundaries(tree);
    buildPenalty(scale);
  }

  // Cheapest counted-command tiling of the raw span [from,to), O(span). Queried over
  // raw positions, so the cost is exact across any collapsed-run interior. 0 when
  // from>=to.
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
};

// FNV-1a over a byte range; O(1) unit-match checks.
uint64_t fnv1a(string_view s) {
  uint64_t h = 1469598103934665603ull;
  for (char c : s) {
    h ^= (unsigned char)c;
    h *= 1099511628211ull;
  }
  return h;
}

// The DP's coordinate system. `collapse=false` is plain char-level (every position
// a unit) — the exact baseline. `collapse=true` skips the interior of matched runs
// the optimum provably keeps (type(run) > move(run) + diffOpenPenalty), so the unit
// count is diff-sized. Changed regions and short/spannable matched runs stay
// char-level, so the DP over these units still explores every alignment within a
// changed region and is exact there; only provably-kept runs are skipped.
class PositionMap {
public:
  PositionMap(const Tree& oldTree, const Tree& newTree, const Lines& oldLines,
              const Lines& newLines, const Config& config, const CostOptions& options,
              bool collapse) {
    const string& A = oldTree.text;
    const string& B = newTree.text;
    const int n = (int)A.size();
    const int m = (int)B.size();

    if (!collapse) {
      oldPos_.reserve(n + 1);
      newPos_.reserve(m + 1);
      for (int i = 0; i <= n; i++) oldPos_.push_back(i);
      for (int j = 0; j <= m; j++) newPos_.push_back(j);
      for (int i = 0; i < n; i++) oldSegHash_.push_back(fnv1a(string_view(A).substr(i, 1)));
      for (int j = 0; j < m; j++) newSegHash_.push_back(fnv1a(string_view(B).substr(j, 1)));
      return;
    }

    TilingCost moveOracle(oldTree, options.moveDeleteScale, TilingCost::Kind::Move);
    vector<int> lineStarts;
    for (const Node& nd : oldTree[Level::Line]) lineStarts.push_back(nd.text.begin);
    sort(lineStarts.begin(), lineStarts.end());

    // Split into matched/changed regions via line-level Myers (gaps = matched runs).
    struct Region {
      int oldB, oldE, newB, newE;
      bool matched;
    };
    vector<Region> regions;
    int oldFlat = 0, newFlat = 0;
    for (const DiffState& d : MyersDiff::calculate(oldLines, newLines)) {
      const int diffOldBegin = d.beginPos.line < (int)lineStarts.size()
                                   ? lineStarts[d.beginPos.line] + d.beginPos.col
                                   : n;
      const int gapLen = diffOldBegin - oldFlat;
      if (gapLen > 0) regions.push_back({oldFlat, diffOldBegin, newFlat, newFlat + gapLen, true});
      const int dOldE = diffOldBegin + (int)d.deletedText.size();
      const int dNewB = newFlat + gapLen;
      const int dNewE = dNewB + (int)d.insertedText.size();
      if (dOldE > diffOldBegin || dNewE > dNewB)
        regions.push_back({diffOldBegin, dOldE, dNewB, dNewE, false});
      oldFlat = dOldE;
      newFlat = dNewE;
    }
    if (n - oldFlat > 0 || m - newFlat > 0) regions.push_back({oldFlat, n, newFlat, m, true});

    oldPos_.push_back(0);
    newPos_.push_back(0);
    // A unit spans [oldPos_.back(), oldEnd); a collapsed run is one such unit (its
    // span hashed whole). The Solver prices it via the raw-span oracle, so no
    // per-unit block cost is stored.
    auto addOldUnit = [&](int oldEnd) {
      oldSegHash_.push_back(fnv1a(string_view(A).substr(oldPos_.back(), oldEnd - oldPos_.back())));
      oldPos_.push_back(oldEnd);
    };
    auto addNewUnit = [&](int newEnd) {
      newSegHash_.push_back(fnv1a(string_view(B).substr(newPos_.back(), newEnd - newPos_.back())));
      newPos_.push_back(newEnd);
    };
    auto addCharSpan = [&](int oFrom, int oTo, int nFrom, int nTo) {
      for (int p = oFrom + 1; p <= oTo; p++) addOldUnit(p);
      for (int q = nFrom + 1; q <= nTo; q++) addNewUnit(q);
    };
    for (const Region& rg : regions) {
      if (!rg.matched) {
        addCharSpan(rg.oldB, rg.oldE, rg.newB, rg.newE);
        continue;
      }
      // Collapse the run interior, keeping MATCH_MARGIN char-level cells at each edge.
      // An optimal edit boundary can slide a little into a matched run for an
      // alignment saving (sub-linear nav gain) but only a bounded distance — sliding k
      // chars costs ~k to retype, so beyond the margin it is never optimal. Keeping
      // the margin char-level lets the DP find that boundary (Plan 1 stays the true
      // optimum); the raw-span oracle prices commands across the collapsed interior.
      const int off = rg.newB - rg.oldB;
      const int coreB = rg.oldB + MATCH_MARGIN;
      const int coreE = rg.oldE - MATCH_MARGIN;
      bool collapsed = false;
      if (coreB < coreE) {
        KeyedSequence typed;
        typed.append(string_view(A).substr(coreB, coreE - coreB));
        const double type = RunningEffort(typed.keys, config).getEffort(config);
        const double mv = moveOracle.query(coreB, coreE);
        collapsed = type > mv + options.diffOpenPenalty;
      }
      if (collapsed) {
        addCharSpan(rg.oldB, coreB, rg.newB, coreB + off);
        addOldUnit(coreE);
        addNewUnit(coreE + off);
        addCharSpan(coreE, rg.oldE, coreE + off, rg.newE);
      } else {
        addCharSpan(rg.oldB, rg.oldE, rg.newB, rg.newE);
      }
    }
  }

  int oldUnits() const { return (int)oldPos_.size() - 1; }
  int newUnits() const { return (int)newPos_.size() - 1; }
  int oldRaw(int i) const { return oldPos_[i]; }
  int newRaw(int j) const { return newPos_[j]; }
  int oldIndex(int raw) const {
    return (int)(lower_bound(oldPos_.begin(), oldPos_.end(), raw) - oldPos_.begin());
  }
  TextRange oldRange(int begin, int end) const { return TextRange{oldRaw(begin), oldRaw(end)}; }
  TextRange newRange(int begin, int end) const { return TextRange{newRaw(begin), newRaw(end)}; }

  bool unitMatch(int oldUnitEnd, int newUnitEnd) const {
    return oldRaw(oldUnitEnd) - oldRaw(oldUnitEnd - 1) ==
               newRaw(newUnitEnd) - newRaw(newUnitEnd - 1) &&
           oldSegHash_[oldUnitEnd - 1] == newSegHash_[newUnitEnd - 1];
  }

private:
  vector<int> oldPos_, newPos_;
  vector<uint64_t> oldSegHash_, newSegHash_;
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
  const PositionMap& pos;
  const Config& config;
  CostOptions options;
  string_view A;
  string_view B;
  int n;
  int m;
  int commonSuffix = 0;
  int U;  // old unit count; active old indices run 0..U
  int V;  // new unit count; active new indices run 0..V
  int K;
  int commonPrefix_ = 0, commonSuffix_ = 0;  // raw leading/trailing matched lengths

  vector<vector<vector<Cand>>> gK, dK, fK;  // [activeOld][activeNew] -> up to K candidates
  vector<vector<char>> gDone, dDone;
  // RunningEffort is a monoid with one-key boundary context, so typing effort
  // decomposes exactly: insCost(q,j) = PS_[j] - PS_[q] - cut_[q] over RAW new
  // positions, PS_ the prefix effort of typing B[0:j) and cut_[q] the bigram
  // correction straddling q. The DP translates active new index -> raw.
  vector<double> PS_, cut_;
  TilingCost del;   // counted-tiling delete oracle (dd/de/dw/...), queried over raw spans
  TilingCost move;  // same tiling with bare-motion bases (dw->w, dd->j, x->l)
  mutable unordered_map<long long, double> moveMemo_, delMemo_;

  Solver(const Tree& oldT, const Tree& newT, const PositionMap& positions, const Config& cfg,
         CostOptions opts, int maxPlans)
      : oldTree(oldT), newTree(newT), pos(positions), config(cfg), options(opts),
        A(oldT.text), B(newT.text), n((int)A.size()), m((int)B.size()),
        U(positions.oldUnits()), V(positions.newUnits()), K(max(1, maxPlans)),
        gK(U + 1, vector<vector<Cand>>(V + 1)),
        dK(U + 1, vector<vector<Cand>>(V + 1)),
        fK(U + 1, vector<vector<Cand>>(V + 1)),
        gDone(U + 1, vector<char>(V + 1, 0)),
        dDone(U + 1, vector<char>(V + 1, 0)),
        del(oldT, opts.moveDeleteScale, TilingCost::Kind::Delete),
        move(oldT, opts.moveDeleteScale, TilingCost::Kind::Move) {
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

  // Oracle over ACTIVE old indices, priced by tiling the RAW span [oldRaw(from),
  // oldRaw(to)) — counted commands tile across any collapsed run interior, so a
  // collapsed run and char-level coords give identical costs (this keeps Plan 1 the
  // true optimum). Memoized. This exactness costs an O(span) walk per distinct query,
  // which is why runtime is not fully diff-bound — see comment at calculate().
  double moveCost(int from, int to) const { return rawCost(move, moveMemo_, from, to); }
  double delCost(int from, int to) const { return rawCost(del, delMemo_, from, to); }
  double rawCost(const TilingCost& oracle, unordered_map<long long, double>& memo, int from,
                 int to) const {
    const int a = pos.oldRaw(from), b = pos.oldRaw(to);
    if (a >= b) return 0.0;
    const long long key = (long long)a * (n + 1) + b;
    auto it = memo.find(key);
    if (it != memo.end()) return it->second;
    return memo[key] = oracle.query(a, b);
  }
  double insPrefix(int at) const { return PS_[pos.newRaw(at)]; }
  double insStartTerm(int at) const { return PS_[pos.newRaw(at)] + cut_[pos.newRaw(at)]; }
  double insCost(int from, int to) const {
    return from >= to ? 0.0 : insPrefix(to) - insStartTerm(from);
  }

  // Old unit i-1 matches new unit j-1: equal length and content (hashed).
  bool unitMatch(int i, int j) const {
    return pos.unitMatch(i, j);
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

  // Positioned to START an edit at active (i,j).
  const vector<Cand>& G(int i, int j) {
    if (gDone[i][j]) return gK[i][j];
    vector<Cand>& out = gK[i][j];
    if (pos.oldRaw(i) == pos.newRaw(j) && pos.oldRaw(i) <= commonPrefix_)
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

  // Fill F row by row. insCost(q,j) = PS_[newRaw(j)] - PS_[newRaw(q)] - cut_[newRaw(q)],
  // so a candidate's cost at any later j is its q-local part plus a j-constant; each
  // row keeps two running K-best lists (branch 0 over D(i,q), q<=j; branch 1 over
  // G(i,q), q<j), O(K) per cell, exact. b0 inserts before b1 to match tie order.
  void solveAll() {
    for (int i = 0; i <= U; i++) {
      vector<Cand> b0, b1;
      for (int j = 0; j <= V; j++) {
        const vector<Cand>& d = D(i, j);
        for (int r = 0; r < (int)d.size(); r++)
          insertCand(b0, {d[r].cost - insStartTerm(j), j, r, 0});
        if (j >= 1) {
          const vector<Cand>& g = G(i, j - 1);
          for (int r = 0; r < (int)g.size(); r++)
            insertCand(b1, {g[r].cost - insStartTerm(j - 1), j - 1, r, 1});
        }
        vector<Cand>& out = fK[i][j];
        const double shift = insPrefix(j) + options.diffOpenPenalty;
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
      out.push_back(EditSpan{pos.oldRange(a, i), pos.newRange(q, j)});
      walkG(a, q, dc.rank, out);
    } else {  // pure insertion
      out.push_back(EditSpan{pos.oldRange(i, i), pos.newRange(q, j)});
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
        if (n - pos.oldRaw(i) == m - pos.newRaw(j) && n - pos.oldRaw(i) <= commonSuffix_) {
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

// Hard bound for the dense tables. Future sparse maps should lower U/V before
// this check rather than raising it.
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


// Shared pipeline for calculate/calculateBreakdown.
struct SolvedPipeline {
  std::unique_ptr<Tree> initialTree, goalTree;
  std::unique_ptr<PositionMap> positions;
  std::unique_ptr<Solver> solver;
  vector<PlanSpans> planSpans;
  bool equal = false;  // initial already equals goal
};

SolvedPipeline runPipeline(const Lines& initialLines, const Lines& goalLines,
                           const Config& config, const CostOptions& options,
                           bool needBreakdown) {
  SolvedPipeline out;
  out.initialTree = std::make_unique<Tree>(initialLines);
  out.goalTree = std::make_unique<Tree>(goalLines);
  if (out.initialTree->text == out.goalTree->text) {
    out.equal = true;
    return out;
  }
  // Production collapses matched-run interiors (diff-bound); the breakdown/K-best
  // diagnostic path keeps exact char-level coordinates (small inputs).
  const bool collapse = !needBreakdown && options.collapseRuns;
  out.positions = std::make_unique<PositionMap>(*out.initialTree, *out.goalTree, initialLines,
                                                goalLines, config, options, collapse);
  checkPlannerSize(out.positions->oldUnits(), out.positions->newUnits());
  out.solver = std::make_unique<Solver>(*out.initialTree, *out.goalTree, *out.positions, config,
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
  const SolvedPipeline p = runPipeline(initialLines, goalLines, config, options, false);
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
  const SolvedPipeline p = runPipeline(initialLines, goalLines, config, options, true);
  if (p.equal) return {};
  const PositionMap& pos = *p.positions;
  const Solver& solver = *p.solver;

  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(p.planSpans.size());
  for (const PlanSpans& ps : p.planSpans) {
    CostBreakdown bd;
    double listed = 0.0;
    int prevEnd = -1;  // active old index of the previous region's old end
    for (const EditSpan& span : ps.spans) {
      DiffState diff = diffFromSpan(initialLines, *p.initialTree, *p.goalTree, span);
      const int aBegin = pos.oldIndex(span.oldText.begin);
      const int aEnd = pos.oldIndex(span.oldText.end);
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
