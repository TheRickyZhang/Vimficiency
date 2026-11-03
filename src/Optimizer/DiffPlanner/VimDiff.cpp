#include "VimDiff.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "PlannerCosts.h"
#include "SealMatchedRuns.h"
#include "VimDiffTrace.h"

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
  vector<double> typed, enter, change;  // per goal char; change = enter without the entry key
};

vector<BlockCosts> calculateTransitionCosts(const FlatText& initial, const FlatText& goal,
                                            const Typing& typing, const vector<Block>& blocks,
                                            TilingCost& move) {
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
      c.typed[j] = typing.ins(b.bBegin + j, b.bBegin + j + 1);
      c.change[j] = typing.esc + c.typed[j];
      c.enter[j] = typing.entry + c.change[j];
    }
  }
  return all;
}

// ---- Stage 3: the DP -------------------------------------------------------

// out[i, j]: normal mode, initial [0,i) consumed, goal [0,j) produced; in[i, j]:
// insert mode. Steps are the three families in VimDiffTrace.h.
struct Cand {
  double cost = INF;
  uint16_t pi = 0, pj = 0;  // predecessor cell; in the previous block for CROSS
  Step step = LEADING;

  bool open() const { return step == DELETE || step == EXIT; }  // region in progress
};
static_assert(sizeof(Cand) == 16);

struct Tables {
  Grid<Cand> out, in;
  Tables(int n, int m) : out(n + 1, m + 1), in(n + 1, m + 1) {}
};

void relax(Cand& cell, double cost, Step step, int pi, int pj) {
  if (cost < cell.cost) cell = {cost, (uint16_t)pi, (uint16_t)pj, step};
}

// One multi-source sweep prices every deletion of column j within the block.
// Each arrival lands in out[i, j] and, as a CHANGE, in in[i, j+1].
void relaxDeletes(Tables& t, const Block& b, const BlockCosts& costs, TilingCost& del, int j) {
  del.sweep(
      b.aBegin, b.aEnd, [&](int ri) { return t.out[ri - b.aBegin, j].cost; },
      [&](int ri, const Arrival& a) {
        if (a.cost >= INF) return;
        const int i = ri - b.aBegin, pi = a.seed - b.aBegin;
        relax(t.out[i, j], a.cost, DELETE, pi, j);
        if (j < b.m()) relax(t.in[i, j + 1], a.cost + costs.change[j], CHANGE, pi, j);
      });
}

vector<Tables> solveVimDiff(const vector<Block>& blocks, const vector<BlockCosts>& costs,
                            const FlatText& initial, const FlatText& goal, TilingCost& del) {
  auto matches = [&](int ra, int rb) { return initial.text[ra] == goal.text[rb]; };
  vector<Tables> tables;
  for (int k = 0; k < (int)blocks.size(); k++) {
    const Block& b = blocks[k];
    const BlockCosts& c = costs[k];
    Tables t(b.n(), b.m());
    for (int j = 0; j <= b.m(); j++) {
      for (int i = 0; i <= b.n(); i++) {
        Cand& in = t.in[i, j];
        Cand& out = t.out[i, j];
        if (j > 0) {
          const int pj = j - 1;
          relax(in, t.in[i, pj].cost + c.typed[pj], TYPE, i, pj);
          relax(in, t.out[i, pj].cost + c.enter[pj], ENTER, i, pj);
        }
        if (i == j && i <= c.lead) {
          if (k == 0) {
            relax(out, 0.0, LEADING, 0, 0);
          } else {
            const Tables& pt = tables[k - 1];
            const Block& pb = blocks[k - 1];
            for (int tr = 0; tr < (int)c.cross.size(); tr++) {
              const int pi = pb.n() - tr, pj = pb.m() - tr;
              relax(out, pt.out[pi, pj].cost + c.cross[tr][i], CROSS, pi, pj);
            }
          }
        }
        for (int pi = i - 1, pj = j - 1; pi >= 0 && pj >= 0 && matches(b.aBegin + pi, b.bBegin + pj);
             pi--, pj--)
          relax(out, t.out[pi, pj].cost + c.move[pi][i], MOVE, pi, pj);
        relax(out, in.cost, EXIT, i, j);
      }
      relaxDeletes(t, b, c, del, j);
    }
    tables.push_back(std::move(t));
  }
  return tables;
}

// ---- Stage 4: the plan -----------------------------------------------------

struct Region {  // raw spans: initial [aBegin,aEnd) -> goal [bBegin,bEnd)
  int aBegin, aEnd, bBegin, bEnd;
};

struct RawPlan {
  vector<Region> regions;
  double cost = 0.0;
};

struct Planned {
  FlatText initial, goal;
  Typing typing;
  TilingCost del, move;
  vector<Block> blocks;
  vector<BlockCosts> costs;  // empty for the sealed fallback
  vector<Tables> tables;     // empty for the sealed fallback
  int endI = 0, endJ = 0;    // optimum cell in the last block
  bool sealedFallback = false;
  RawPlan plan;

  Planned(const Lines& initialLines, const Lines& goalLines, const Config& config,
          const CostOptions& o)
      : initial(initialLines), goal(goalLines), typing(goal, config),
        del(initial, o.moveDeleteScale, o.maxPrefixCount, TilingCost::Kind::Delete),
        move(initial, o.moveDeleteScale, o.maxPrefixCount, TilingCost::Kind::Move) {}
};

// Typed effort plus <Esc>, plus the entry key only when no deletion precedes
// the typing (a deletion's change form absorbs it).
double insertCost(const Typing& typing, const Region& r) {
  if (r.bBegin >= r.bEnd) return 0.0;
  return (r.aBegin < r.aEnd ? 0.0 : typing.entry) + typing.esc + typing.ins(r.bBegin, r.bEnd);
}

bool predecessorIsIn(const Cand& c) { return c.step == TYPE || c.step == EXIT; }

const Cand& predecessor(const Tables& t, const Cand& c) {
  return predecessorIsIn(c) ? t.in[c.pi, c.pj] : t.out[c.pi, c.pj];
}

// A region spans from its first delete/type step to the next move (or the end).
// `visit(k, i, j, inTable)` sees every cell on the path, optimum first.
template<class Visit>
vector<Region> walk(const vector<Tables>& tables, const vector<Block>& blocks, int k, int i,
                    int j, Visit&& visit) {
  vector<Region> regions;
  const Cand* c = &tables[k].out[i, j];
  visit(k, i, j, false);
  int ca = blocks[k].aBegin + i, cb = blocks[k].bBegin + j;
  while (c->step != LEADING) {
    const int pk = c->step == CROSS ? k - 1 : k;
    const Cand& pred = predecessor(tables[pk], *c);
    visit(pk, c->pi, c->pj, predecessorIsIn(*c));
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

// One region per block. Always a valid plan — blocks are exactly the spans
// between sealed identical runs — but never weighed against merging or
// splitting them, so it is only the over-budget fallback.
void sealedPartition(Planned& p) {
  p.sealedFallback = true;
  int prevEnd = -1;
  for (const Block& b : p.blocks) {
    const Region r{b.aBegin, b.aEnd, b.bBegin, b.bEnd};
    p.plan.cost += p.del.query(r.aBegin, r.aEnd) + insertCost(p.typing, r);
    if (prevEnd >= 0) p.plan.cost += p.move.query(prevEnd, r.aBegin);
    prevEnd = r.aEnd;
    p.plan.regions.push_back(r);
  }
}

// The optimum ends anywhere on the last block's trailing matched diagonal.
void dpPlan(Planned& p) {
  p.costs = calculateTransitionCosts(p.initial, p.goal, p.typing, p.blocks, p.move);
  p.tables = solveVimDiff(p.blocks, p.costs, p.initial, p.goal, p.del);
  const int last = (int)p.blocks.size() - 1;
  const Block& b = p.blocks[last];
  const Tables& t = p.tables[last];
  int bestT = 0;
  for (int tr = 1; tr <= p.costs.back().trail; tr++)
    if (t.out[b.n() - tr, b.m() - tr].cost < t.out[b.n() - bestT, b.m() - bestT].cost) bestT = tr;
  p.endI = b.n() - bestT;
  p.endJ = b.m() - bestT;
  p.plan = {walk(p.tables, p.blocks, last, p.endI, p.endJ, [](int, int, int, bool) {}),
            t.out[p.endI, p.endJ].cost};
}

Planned plan(const Lines& initialLines, const Lines& goalLines, const Config& config,
             const CostOptions& options) {
  Planned p(initialLines, goalLines, config, options);
  if (p.initial.text == p.goal.text) return p;
  p.blocks = sealMatchedRuns(p.initial, p.goal, p.typing, p.del, p.move, initialLines, goalLines);
  long long cells = 0;
  bool tooWide = false;  // Cand::pi/pj
  for (const Block& b : p.blocks) {
    cells += (long long)(b.n() + 1) * (b.m() + 1);
    tooWide |= b.n() > numeric_limits<uint16_t>::max() || b.m() > numeric_limits<uint16_t>::max();
  }
  if (tooWide || cells > options.maxPlannerCells) sealedPartition(p);
  else dpPlan(p);
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

CostBreakdown breakdownOf(Planned& p, const Lines& initialLines) {
  CostBreakdown bd;
  int prevEnd = -1;
  for (const Region& r : p.plan.regions) {
    const double del = p.del.query(r.aBegin, r.aEnd);
    const double ins = insertCost(p.typing, r);
    const double mv = prevEnd < 0 ? 0.0 : p.move.query(prevEnd, r.aBegin);
    prevEnd = r.aEnd;
    bd.total += del + ins + mv;
    bd.regions.push_back(RegionBreakdown{diffFromRegion(initialLines, p, r), del, ins, mv});
  }
  return bd;
}

TraceCell traceCell(const Cand& c) { return {c.cost, c.step, c.pi, c.pj}; }

}  // namespace

Plan calculate(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  const Planned p = plan(initialLines, goalLines, config, options);
  Plan result;
  result.cost = p.plan.cost;
  for (const Region& r : p.plan.regions) result.diffs.push_back(diffFromRegion(initialLines, p, r));
  return result;
}

CostBreakdown calculateBreakdown(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  Planned p = plan(initialLines, goalLines, config, options);
  return breakdownOf(p, initialLines);
}

Trace trace(
    const Lines& initialLines,
    const Lines& goalLines,
    const Config& config,
    CostOptions options) {
  Planned p = plan(initialLines, goalLines, config, options);
  Trace t;
  t.sealedFallback = p.sealedFallback;
  t.rawCells = (long long)(p.initial.text.size() + 1) * (p.goal.text.size() + 1);
  for (int k = 0; k < (int)p.blocks.size(); k++) {
    const Block& b = p.blocks[k];
    TraceBlock tb{b};
    const int n = b.n(), m = b.m();
    t.dpCells += (long long)(n + 1) * (m + 1);
    if (!p.sealedFallback) {
      tb.lead = p.costs[k].lead;
      tb.trail = p.costs[k].trail;
      tb.out.reserve((n + 1) * (m + 1));
      tb.in.reserve((n + 1) * (m + 1));
      for (int i = 0; i <= n; i++)
        for (int j = 0; j <= m; j++) {
          tb.out.push_back(traceCell(p.tables[k].out[i, j]));
          tb.in.push_back(traceCell(p.tables[k].in[i, j]));
        }
    }
    t.blocks.push_back(std::move(tb));
  }
  if (!p.sealedFallback && !p.blocks.empty()) {
    walk(p.tables, p.blocks, (int)p.blocks.size() - 1, p.endI, p.endJ,
         [&](int k, int i, int j, bool inTable) { t.path.push_back({k, i, j, inTable}); });
    reverse(t.path.begin(), t.path.end());
  }
  t.breakdown = breakdownOf(p, initialLines);
  return t;
}

}  // namespace VimDiff
