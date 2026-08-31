#include "VimDiff.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PlannerCosts.h"
#include "SealMatchedRuns.h"
#include "Utils/Debug.h"

using namespace std;

namespace VimDiff {
namespace {

// Column-major (i contiguous) 2D storage in one allocation, matching the
// `for j { for i }` sweep order of the DP.
template<class T>
class Grid {
public:
  Grid(int rows, int cols) : rows_(rows), cells_((size_t)rows * cols) {}
  T& operator[](int i, int j) { return cells_[(size_t)j * rows_ + i]; }
  const T& operator[](int i, int j) const { return cells_[(size_t)j * rows_ + i]; }

private:
  int rows_;
  vector<T> cells_;
};

// ---- Stage 2: transition costs ---------------------------------------------

struct BlockCosts {
  int lead = 0, trail = 0;       // matched diagonal length at the block's start / end
  vector<vector<double>> move;   // [pi][i]
  vector<vector<double>> cross;  // [t][i]: from the previous block's (n-t, m-t) to (i,i)
  vector<double> typed, enter, change;  // per goal unit; change = enter without the entry key
};

vector<BlockCosts> calculateTransitionCosts(const FlatText& initial, const FlatText& goal,
                                            const Typing& typing, const vector<Block>& blocks,
                                            const CostOptions& options) {
  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  auto matches = [&](int ra, int rb) { return initial.text[ra] == goal.text[rb]; };

  vector<BlockCosts> all(blocks.size());
  for (int k = 0; k < (int)blocks.size(); k++) {
    const Block& b = blocks[k];
    BlockCosts& c = all[k];
    const int n = b.n(), m = b.m();
    while (c.lead < min(n, m) && matches(b.aBegin + c.lead, b.bBegin + c.lead)) c.lead++;
    while (c.trail < min(n, m) && matches(b.aEnd - 1 - c.trail, b.bEnd - 1 - c.trail)) c.trail++;

    c.move.assign(n + 1, vector<double>(n + 1, 0.0));
    for (int pi = 0; pi < n; pi++)
      move.sweep(b.aBegin + pi, b.aEnd,
                 [&](int ri, double cost) { c.move[pi][ri - b.aBegin] = cost; });

    if (k > 0) {
      const Block& prev = blocks[k - 1];
      const int trail = all[k - 1].trail;
      c.cross.assign(trail + 1, vector<double>(c.lead + 1, 0.0));
      for (int t = 0; t <= trail; t++)
        move.sweep(prev.aEnd - t, b.aBegin + c.lead, [&](int ri, double cost) {
          if (ri >= b.aBegin) c.cross[t][ri - b.aBegin] = cost;
        });
    }

    c.typed.resize(m);
    c.enter.resize(m);
    c.change.resize(m);
    for (int j = 0; j < m; j++) {
      const int rj = b.bBegin + j;
      c.typed[j] = typing.PS[rj + 1] - typing.PS[rj];
      c.change[j] = typing.esc - typing.cut[rj] + c.typed[j];
      c.enter[j] = typing.entry + c.change[j];
    }
  }
  return all;
}

// ---- Stage 3: the DP -------------------------------------------------------

// out[i, j]: normal mode, initial [0,i) consumed, goal [0,j) produced; in[i, j]: insert mode.
// A cell keeps up to `maxPlans` candidates, one per partition key. Everything is
// templated on the slot capacity K so the single-plan instantiation carries no
// keys at all: with one candidate per cell there is nothing to tell apart.
// CHANGE is a deletion that lands straight in insert mode — the `c` form — and
// so skips the entry key. It is an edge, not a state: nothing can sit between
// a delete and the insert it merges with.
enum Step : int8_t { LEADING, MOVE, CROSS, DELETE, CHANGE, ENTER, TYPE, EXIT };

// Partition fingerprint: XOR of mixed marks at every cell a region opened or
// closed at, so every path with the same regions shares a key and a cell's K
// slots hold K distinct partitions. `pkey` names the predecessor's entry within
// its cell.
template<int K> struct Keys { uint32_t key = 0, pkey = 0; };
template<> struct Keys<1> {};

template<int K>
struct Cand {
  double cost;
  uint16_t pi, pj;  // predecessor cell; in the previous block for CROSS
  Step step;
  [[no_unique_address]] Keys<K> keys;

  bool open() const { return step == DELETE || step == EXIT; }  // region in progress
};
static_assert(sizeof(Cand<1>) == 16);
static_assert(sizeof(Cand<MAX_PLANS_CAP>) == 24);

// Cost-ascending candidates, one per key.
template<int K>
struct Cell {
  array<Cand<K>, K> c;
  uint8_t n = 0;
  const Cand<K>* begin() const { return c.data(); }
  const Cand<K>* end() const { return c.data() + n; }
  bool empty() const { return n == 0; }
  void clear() { n = 0; }
};
// The candidate itself; INF cost means empty.
template<>
struct Cell<1> {
  Cand<1> c{INF, 0, 0, LEADING, {}};
  const Cand<1>* begin() const { return &c; }
  const Cand<1>* end() const { return &c + (c.cost < INF ? 1 : 0); }
  bool empty() const { return c.cost >= INF; }
  void clear() { c.cost = INF; }
};
static_assert(sizeof(Cell<1>) == 16);

uint64_t mix64(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}
uint32_t mark(int ra, int rb, bool close) {
  return (uint32_t)mix64(((uint64_t)(uint32_t)ra << 33) | ((uint64_t)(uint32_t)rb << 1) |
                         (close ? 1 : 0));
}
// Keys of the candidate derived from `c` by a step that opens a region at
// (ra, rb), closes one there, or stays inside one. Nothing for K == 1.
template<int K>
Keys<K> openKeys(const Cand<K>& c, int ra, int rb) {
  if constexpr (K == 1) return {};
  else return {c.open() ? c.keys.key : c.keys.key ^ mark(ra, rb, false), c.keys.key};
}
template<int K>
Keys<K> closeKeys(const Cand<K>& c, int ra, int rb) {
  if constexpr (K == 1) return {};
  else return {c.open() ? c.keys.key ^ mark(ra, rb, true) : c.keys.key, c.keys.key};
}
template<int K>
Keys<K> sameKeys(const Cand<K>& c) {
  if constexpr (K == 1) return {};
  else return {c.keys.key, c.keys.key};
}

template<int K>
struct Tables {
  int maxPlans;  // runtime cap <= K on entries per cell
  Grid<Cell<K>> out, in;

  Tables(int n, int m, int maxPlans) : maxPlans(maxPlans), out(n + 1, m + 1), in(n + 1, m + 1) {}

  void relax(Cell<K>& cell, const Cand<K>& from, double add, Step step, uint16_t pi, uint16_t pj,
             Keys<K> keys) const {
    insert(cell, Cand<K>{from.cost + add, pi, pj, step, keys});
  }

  void insert(Cell<K>& cell, const Cand<K>& c) const {
    if constexpr (K == 1) {
      if (c.cost < cell.c.cost) cell.c = c;
    } else {
      for (int x = 0; x < cell.n; x++) {
        if (cell.c[x].keys.key != c.keys.key) continue;
        if (cell.c[x].cost <= c.cost) return;
        for (int y = x + 1; y < cell.n; y++) cell.c[y - 1] = cell.c[y];
        cell.n--;
        break;
      }
      if (cell.n == maxPlans && c.cost >= cell.c[cell.n - 1].cost) return;
      int pos = 0;
      while (pos < cell.n && cell.c[pos].cost <= c.cost) pos++;
      if (cell.n < maxPlans) cell.n++;
      for (int y = cell.n - 1; y > pos; y--) cell.c[y] = cell.c[y - 1];
      cell.c[pos] = c;
    }
  }
};

// One multi-source sweep prices every deletion of column j within the block.
// Each arrival lands in out[i, j] and, as a CHANGE, in in[i, j+1].
template<int K>
void relaxDeletes(Tables<K>& t, const Block& b, const BlockCosts& costs, TilingCost& del,
                  TilingCost::Scratch<Cell<K>>& scratch, vector<Cell<K>>& seeds, int j) {
  struct SweepOps {
    using V = Cell<K>;
    const Tables<K>& t;
    const V& inf() const {
      static const V EMPTY{};
      return EMPTY;
    }
    void reset(V& v) const { v.clear(); }
    void relax(V& acc, const V& base, double add) const {
      for (Cand<K> c : base) {
        c.cost += add;
        t.insert(acc, c);
      }
    }
  };
  int begin = -1;
  for (int i = 0; i <= b.n(); i++) {
    seeds[i].clear();
    for (const Cand<K>& c : t.out[i, j])
      t.relax(seeds[i], c, 0.0, DELETE, i, j, openKeys(c, b.aBegin + i, b.bBegin + j));
    if (begin < 0 && !seeds[i].empty()) begin = b.aBegin + i;
  }
  if (begin < 0) return;
  del.sweep(
      SweepOps{t}, scratch, begin, b.aEnd,
      [&](int ri) -> const Cell<K>& { return seeds[ri - b.aBegin]; },
      [&](int ri, const Cell<K>& e) {
        const int i = ri - b.aBegin;
        for (const Cand<K>& c : e) t.insert(t.out[i, j], c);
        if (j == b.m()) return;
        for (Cand<K> c : e) {
          c.cost += costs.change[j];
          c.step = CHANGE;
          t.insert(t.in[i, j + 1], c);
        }
      });
}

template<int K>
vector<Tables<K>> solveVimDiff(const vector<Block>& blocks, const vector<BlockCosts>& costs,
                               const FlatText& initial, const FlatText& goal, TilingCost& del,
                               int maxPlans) {
  TilingCost::Scratch<Cell<K>> scratch = del.makeScratch<Cell<K>>({});
  auto matches = [&](int ra, int rb) { return initial.text[ra] == goal.text[rb]; };
  vector<Tables<K>> tables;
  for (int k = 0; k < (int)blocks.size(); k++) {
    const Block& b = blocks[k];
    const BlockCosts& c = costs[k];
    Tables<K> t(b.n(), b.m(), maxPlans);
    vector<Cell<K>> seeds(b.n() + 1);
    for (int j = 0; j <= b.m(); j++) {
      for (int i = 0; i <= b.n(); i++) {
        Cell<K>& in = t.in[i, j];
        Cell<K>& out = t.out[i, j];
        const int ra = b.aBegin + i, rb = b.bBegin + j;
        if (j > 0) {
          const int pj = j - 1;
          for (const Cand<K>& x : t.in[i, pj]) t.relax(in, x, c.typed[pj], TYPE, i, pj, sameKeys(x));
          for (const Cand<K>& x : t.out[i, pj])
            t.relax(in, x, c.enter[pj], ENTER, i, pj, openKeys(x, ra, rb - 1));
        }
        if (i == j && i <= c.lead) {
          if (k == 0) {
            t.insert(out, Cand<K>{0.0, 0, 0, LEADING, {}});
          } else {
            const Tables<K>& pt = tables[k - 1];
            const Block& pb = blocks[k - 1];
            for (int tr = 0; tr < (int)c.cross.size(); tr++) {
              const int pi = pb.n() - tr, pj = pb.m() - tr;
              for (const Cand<K>& x : pt.out[pi, pj])
                t.relax(out, x, c.cross[tr][i], CROSS, pi, pj,
                        closeKeys(x, pb.aBegin + pi, pb.bBegin + pj));
            }
          }
        }
        for (int pi = i - 1, pj = j - 1; pi >= 0 && pj >= 0 && matches(ra - 1 - (i - 1 - pi), rb - 1 - (j - 1 - pj));
             pi--, pj--)
          for (const Cand<K>& x : t.out[pi, pj])
            t.relax(out, x, c.move[pi][i], MOVE, pi, pj, closeKeys(x, b.aBegin + pi, b.bBegin + pj));
        for (const Cand<K>& x : in) t.relax(out, x, 0.0, EXIT, i, j, sameKeys(x));
      }
      relaxDeletes(t, b, c, del, scratch, seeds, j);
    }
    tables.push_back(std::move(t));
  }
  return tables;
}

// ---- Stage 4: plans --------------------------------------------------------

struct Region {  // raw spans: initial [aBegin,aEnd) -> goal [bBegin,bEnd)
  int aBegin, aEnd, bBegin, bEnd;
  bool operator==(const Region&) const = default;
};

struct RawPlan {
  vector<Region> regions;
  double cost = 0.0;
};

// Typed effort plus <Esc>, plus the entry key only when no deletion precedes
// the typing (a deletion's change form absorbs it).
double insertCost(const Typing& typing, const Region& r) {
  if (r.bBegin >= r.bEnd) return 0.0;
  return (r.aBegin < r.aEnd ? 0.0 : typing.entry) + typing.esc + typing.ins(r.bBegin, r.bEnd);
}

template<int K>
const Cand<K>& predecessor(const Tables<K>& t, const Cand<K>& c) {
  const bool predOut = c.step != TYPE && c.step != EXIT;
  const Cell<K>& cell = predOut ? t.out[c.pi, c.pj] : t.in[c.pi, c.pj];
  if constexpr (K == 1) {
    return cell.c;
  } else {
    for (const Cand<K>& p : cell)
      if (p.keys.key == c.keys.pkey) return p;
    CHECK(false, "VimDiff: predecessor candidate missing");
    return c;
  }
}

// A region spans from its first delete/type step to the next move (or the end).
template<int K>
vector<Region> walk(const vector<Tables<K>>& tables, const vector<Block>& blocks, int k, int i,
                    int j, const Cand<K>* c) {
  vector<Region> regions;
  int ca = blocks[k].aBegin + i, cb = blocks[k].bBegin + j;
  while (c->step != LEADING) {
    const int pk = c->step == CROSS ? k - 1 : k;
    const Cand<K>& pred = predecessor(tables[pk], *c);
    const int pa = blocks[pk].aBegin + c->pi, pb = blocks[pk].bBegin + c->pj;
    if (c->step == MOVE || c->step == CROSS) {
      if (pred.open()) {
        ca = pa;
        cb = pb;
      }
    } else if ((c->step == DELETE || c->step == CHANGE || c->step == ENTER) && !pred.open()) {
      regions.push_back({pa, ca, pb, cb});
    }
    c = &pred;
    k = pk;
  }
  reverse(regions.begin(), regions.end());
  return regions;
}

// Identical-replace regions are dropped: never optimal, rejected downstream.
template<int K>
vector<RawPlan> reconstructPlans(const vector<Tables<K>>& tables, const vector<Block>& blocks,
                                 int trail, const FlatText& initial, const FlatText& goal) {
  struct Top {
    double cost;
    int i, j;
    const Cand<K>* c;
  };
  const int last = (int)blocks.size() - 1;
  const Block& b = blocks[last];
  vector<Top> tops;
  for (int t = trail; t >= 0; t--)
    for (const Cand<K>& c : tables[last].out[b.n() - t, b.m() - t])
      if (c.step != LEADING) tops.push_back({c.cost, b.n() - t, b.m() - t, &c});
  stable_sort(tops.begin(), tops.end(),
              [](const Top& lhs, const Top& rhs) { return lhs.cost < rhs.cost; });

  vector<RawPlan> plans;
  for (const Top& top : tops) {
    if ((int)plans.size() == tables[last].maxPlans) break;
    vector<Region> regions = walk(tables, blocks, last, top.i, top.j, top.c);
    const bool degenerate = any_of(regions.begin(), regions.end(), [&](const Region& r) {
      return string_view(initial.text).substr(r.aBegin, r.aEnd - r.aBegin) ==
             string_view(goal.text).substr(r.bBegin, r.bEnd - r.bBegin);
    });
    const bool seen = any_of(plans.begin(), plans.end(),
                             [&](const RawPlan& plan) { return plan.regions == regions; });
    if (!degenerate && !seen) plans.push_back({std::move(regions), top.cost});
  }
  return plans;
}

// ---- Pipeline --------------------------------------------------------------

struct Planned {
  FlatText initial, goal;
  Typing typing;
  vector<RawPlan> plans;
};

// One region per block. Always a valid plan — blocks are exactly the spans
// between sealed identical runs — but never weighed against merging or
// splitting them, so it is only the over-budget fallback.
RawPlan sealedPartition(const Planned& p, const vector<Block>& blocks, TilingCost& del,
                        TilingCost& move) {
  RawPlan plan;
  int prevEnd = -1;
  for (const Block& b : blocks) {
    const Region r{b.aBegin, b.aEnd, b.bBegin, b.bEnd};
    plan.cost += del.query(r.aBegin, r.aEnd) + insertCost(p.typing, r);
    if (prevEnd >= 0) plan.cost += move.query(prevEnd, r.aBegin);
    prevEnd = r.aEnd;
    plan.regions.push_back(r);
  }
  return plan;
}

template<int K>
vector<RawPlan> dpPlans(const Planned& p, const vector<Block>& blocks, TilingCost& del,
                        const CostOptions& options) {
  const vector<BlockCosts> costs =
      calculateTransitionCosts(p.initial, p.goal, p.typing, blocks, options);
  const vector<Tables<K>> tables =
      solveVimDiff<K>(blocks, costs, p.initial, p.goal, del, max(1, options.maxPlans));
  return reconstructPlans(tables, blocks, costs.back().trail, p.initial, p.goal);
}

Planned plan(const Lines& initialLines, const Lines& goalLines, const Config& config,
             const CostOptions& options) {
  CHECK(options.maxPlans <= MAX_PLANS_CAP, "VimDiff: maxPlans above MAX_PLANS_CAP");
  Planned p{FlatText(initialLines), FlatText(goalLines), Typing(FlatText(goalLines), config)};
  if (p.initial.text == p.goal.text) return p;
  const vector<Block> blocks =
      sealMatchedRuns(p.initial, p.goal, p.typing, initialLines, goalLines, config, options);
  TilingCost del(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  const bool multi = options.maxPlans > 1;
  const long long cellBytes = multi ? sizeof(Cell<MAX_PLANS_CAP>) : sizeof(Cell<1>);
  long long cells = 0;
  bool tooWide = false;  // Cand::pi/pj
  for (const Block& b : blocks) {
    cells += (long long)(b.n() + 1) * (b.m() + 1);
    tooWide |= b.n() > numeric_limits<uint16_t>::max() || b.m() > numeric_limits<uint16_t>::max();
  }
  if (tooWide || cells * cellBytes > options.maxPlannerCells * (long long)sizeof(Cell<1>)) {
    TilingCost move(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
    p.plans.push_back(sealedPartition(p, blocks, del, move));
    return p;
  }
  p.plans = multi ? dpPlans<MAX_PLANS_CAP>(p, blocks, del, options)
                  : dpPlans<1>(p, blocks, del, options);
  return p;
}

DiffState diffFromRegion(const Lines& initialLines, const Planned& p, const Region& r) {
  string deletedText = p.initial.text.substr(r.aBegin, r.aEnd - r.aBegin);
  string insertedText = p.goal.text.substr(r.bBegin, r.bEnd - r.bBegin);
  CursorPos begin = DiffText::flatIndexToPosition(r.aBegin, p.initial.text);
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
  const Planned p = plan(initialLines, goalLines, config, options);
  vector<Plan> plans;
  plans.reserve(p.plans.size());
  for (const RawPlan& rp : p.plans) {
    Plan result;
    result.cost = rp.cost;
    for (const Region& r : rp.regions) result.diffs.push_back(diffFromRegion(initialLines, p, r));
    plans.push_back(std::move(result));
  }
  return plans;
}

vector<CostBreakdown> calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const Planned p = plan(initialLines, goalLines, config, options);
  TilingCost del(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Delete);
  TilingCost move(p.initial, options.moveDeleteScale, options.maxPrefixCount, TilingCost::Kind::Move);
  vector<CostBreakdown> breakdowns;
  breakdowns.reserve(p.plans.size());
  for (const RawPlan& rp : p.plans) {
    CostBreakdown bd;
    int prevEnd = -1;
    for (const Region& r : rp.regions) {
      const double delCost = del.query(r.aBegin, r.aEnd);
      const double ins = insertCost(p.typing, r);
      const double mv = prevEnd < 0 ? 0.0 : move.query(prevEnd, r.aBegin);
      prevEnd = r.aEnd;
      bd.total += delCost + ins + mv;
      bd.regions.push_back(RegionBreakdown{diffFromRegion(initialLines, p, r), delCost, ins, mv});
    }
    breakdowns.push_back(std::move(bd));
  }
  return breakdowns;
}

}  // namespace VimDiff
