// tests/Debug/VimDiffTraceExport.cpp
//
// Exports a full DP trace of the VimDiff planner for one (initial, goal)
// example, feeding the README dp-walkthrough animation (anim/dp-walkthrough).
//
// Re-runs the K=1 recurrence naively with the production cost oracles
// (TilingCost, Typing, sealMatchedRuns) while logging every relaxation, then
// asserts the traced optimum against VimDiff::calculateBreakdown so the trace
// can never drift from the production planner.
//
// Run:
//   VIMFY_TRACE_OUT=anim/dp-walkthrough/trace.json \
//     ./build/tests/vimfy_debug --gtest_filter='VimDiffTraceExport.*'
// Optional: VIMFY_TRACE_INITIAL / VIMFY_TRACE_GOAL override the example
// (literal "\n" for newlines). Without VIMFY_TRACE_OUT the equality check
// still runs; nothing is written.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/PlannerCosts.h"
#include "Optimizer/DiffPlanner/SealMatchedRuns.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Types/Lines.h"

using namespace std;
using namespace VimDiff;
using json = nlohmann::json;

namespace {

enum class TStep { Leading, Move, Cross, Delete, Change, Enter, Type, Exit };

const char* stepName(TStep s) {
  switch (s) {
    case TStep::Leading: return "LEADING";
    case TStep::Move: return "MOVE";
    case TStep::Cross: return "CROSS";
    case TStep::Delete: return "DELETE";
    case TStep::Change: return "CHANGE";
    case TStep::Enter: return "ENTER";
    case TStep::Type: return "TYPE";
    case TStep::Exit: return "EXIT";
  }
  return "?";
}

struct TraceCand {
  double cost = INF;
  TStep step = TStep::Leading;
  int pk = 0, pi = 0, pj = 0;
  bool predIn = false;

  bool open() const { return step == TStep::Delete || step == TStep::Exit; }
};

using TraceGrid = vector<vector<TraceCand>>;  // [i][j]

struct BlockTables {
  int lead = 0, trail = 0;
  TraceGrid out, in;
};

struct Tracer {
  json events = json::array();

  void relax(TraceCand& cell, const TraceCand& from, double add, TStep step, int k, bool intoIn,
             int i, int j, int pk, bool predIn, int pi, int pj) {
    const double total = from.cost + add;
    const bool accepted = total < cell.cost;
    events.push_back({{"k", k},
                      {"table", intoIn ? "in" : "out"},
                      {"i", i},
                      {"j", j},
                      {"step", stepName(step)},
                      {"pk", pk},
                      {"ptable", predIn ? "in" : "out"},
                      {"pi", pi},
                      {"pj", pj},
                      {"add", add},
                      {"total", total},
                      {"accepted", accepted}});
    if (accepted) cell = TraceCand{total, step, pk, pi, pj, predIn};
  }
};

string unescapeNewlines(const string& s) {
  string result;
  for (size_t i = 0; i < s.size(); i++) {
    if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'n') {
      result += '\n';
      i++;
    } else {
      result += s[i];
    }
  }
  return result;
}

struct RawRegion {
  int aBegin, aEnd, bBegin, bEnd;
};

}  // namespace

TEST(VimDiffTraceExport, ExportExample) {
  const char* initEnv = getenv("VIMFY_TRACE_INITIAL");
  const char* goalEnv = getenv("VIMFY_TRACE_GOAL");
  const Lines initialLines = Lines::unflatten(unescapeNewlines(initEnv ? initEnv : "aa b cc"));
  const Lines goalLines = Lines::unflatten(unescapeNewlines(goalEnv ? goalEnv : "xx b zz"));
  const Config config = Config::uniform();
  const CostOptions options{};

  const FlatText initial(initialLines), goal(goalLines);
  ASSERT_NE(initial.text, goal.text);
  const Typing typing(goal, config);
  const vector<Block> blocks =
      sealMatchedRuns(initial, goal, typing, initialLines, goalLines, config, options);
  TilingCost del(initial, options.moveDeleteScale, options.maxPrefixCount,
                 TilingCost::Kind::Delete);
  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount,
                  TilingCost::Kind::Move);
  auto matches = [&](int ra, int rb) { return initial.text[ra] == goal.text[rb]; };

  // The K=1 DP, naive form: same relaxations as solveVimDiff, one event each.
  Tracer tracer;
  vector<BlockTables> tables;
  for (int k = 0; k < (int)blocks.size(); k++) {
    const Block& b = blocks[k];
    const int n = b.n(), m = b.m();
    BlockTables t;
    while (t.lead < min(n, m) && matches(b.aBegin + t.lead, b.bBegin + t.lead)) t.lead++;
    while (t.trail < min(n, m) && matches(b.aEnd - 1 - t.trail, b.bEnd - 1 - t.trail)) t.trail++;
    t.out.assign(n + 1, vector<TraceCand>(m + 1));
    t.in.assign(n + 1, vector<TraceCand>(m + 1));

    for (int j = 0; j <= m; j++) {
      for (int i = 0; i <= n; i++) {
        if (j > 0) {
          const int rj = b.bBegin + j - 1;
          const double typed = typing.PS[rj + 1] - typing.PS[rj];
          const double enter = typing.entry + typing.esc - typing.cut[rj] + typed;
          if (t.in[i][j - 1].cost < INF)
            tracer.relax(t.in[i][j], t.in[i][j - 1], typed, TStep::Type, k, true, i, j, k, true, i,
                         j - 1);
          if (t.out[i][j - 1].cost < INF)
            tracer.relax(t.in[i][j], t.out[i][j - 1], enter, TStep::Enter, k, true, i, j, k, false,
                         i, j - 1);
        }
        if (i == j && i <= t.lead) {
          if (k == 0) {
            tracer.relax(t.out[i][j], TraceCand{0.0, TStep::Leading, 0, 0, 0, false}, 0.0,
                         TStep::Leading, k, false, i, j, k, false, 0, 0);
            t.out[i][j].step = TStep::Leading;  // relax records pred; roots stay LEADING
          } else {
            const Block& pb = blocks[k - 1];
            const BlockTables& pt = tables[k - 1];
            for (int tr = 0; tr <= pt.trail; tr++) {
              const int pi = pb.n() - tr, pj = pb.m() - tr;
              if (pt.out[pi][pj].cost < INF)
                tracer.relax(t.out[i][j], pt.out[pi][pj],
                             move.query(pb.aEnd - tr, b.aBegin + i), TStep::Cross, k, false, i, j,
                             k - 1, false, pi, pj);
            }
          }
        }
        const int ra = b.aBegin + i, rb = b.bBegin + j;
        for (int pi = i - 1, pj = j - 1;
             pi >= 0 && pj >= 0 && matches(ra - 1 - (i - 1 - pi), rb - 1 - (j - 1 - pj));
             pi--, pj--)
          if (t.out[pi][pj].cost < INF)
            tracer.relax(t.out[i][j], t.out[pi][pj], move.query(b.aBegin + pi, b.aBegin + i),
                         TStep::Move, k, false, i, j, k, false, pi, pj);
        if (t.in[i][j].cost < INF)
          tracer.relax(t.out[i][j], t.in[i][j], 0.0, TStep::Exit, k, false, i, j, k, true, i, j);
      }
      // Deletion pass: seeds are the column's pre-delete out cells, matching
      // the production multi-source sweep (tiling subsumes chained deletes).
      vector<TraceCand> seeds;
      for (int i = 0; i <= n; i++) seeds.push_back(t.out[i][j]);
      for (int i = 1; i <= n; i++)
        for (int pi = 0; pi < i; pi++) {
          if (seeds[pi].cost >= INF) continue;
          const double delCost = del.query(b.aBegin + pi, b.aBegin + i);
          tracer.relax(t.out[i][j], seeds[pi], delCost, TStep::Delete, k, false, i, j, k, false,
                       pi, j);
          if (j == m) continue;
          const int rj = b.bBegin + j;
          const double change = typing.esc - typing.cut[rj] + typing.PS[rj + 1] - typing.PS[rj];
          tracer.relax(t.in[i][j + 1], seeds[pi], delCost + change, TStep::Change, k, true, i,
                       j + 1, k, false, pi, j);
        }
    }
    tables.push_back(std::move(t));
  }

  // Traced optimum over the last block's trailing matched diagonal.
  const int last = (int)blocks.size() - 1;
  const Block& lb = blocks[last];
  int topI = -1, topJ = -1;
  double best = INF;
  for (int tr = tables[last].trail; tr >= 0; tr--) {
    const TraceCand& c = tables[last].out[lb.n() - tr][lb.m() - tr];
    if (c.cost < INF && c.step != TStep::Leading && c.cost < best) {
      best = c.cost;
      topI = lb.n() - tr;
      topJ = lb.m() - tr;
    }
  }
  ASSERT_GE(topI, 0);

  // Traceback, mirroring VimDiff's walk(): a region spans from its first
  // delete/type step to the next move (or the end).
  vector<RawRegion> regions;
  json path = json::array();
  {
    int k = last, i = topI, j = topJ;
    TraceCand c = tables[k].out[i][j];
    int ca = blocks[k].aBegin + i, cb = blocks[k].bBegin + j;
    path.push_back({{"k", k}, {"table", "out"}, {"i", i}, {"j", j},
                    {"step", stepName(c.step)}, {"cost", c.cost}});
    while (c.step != TStep::Leading) {
      const int pk = c.pk;
      const TraceCand& pred =
          c.predIn ? tables[pk].in[c.pi][c.pj] : tables[pk].out[c.pi][c.pj];
      const int pa = blocks[pk].aBegin + c.pi, pb = blocks[pk].bBegin + c.pj;
      if (c.step == TStep::Move || c.step == TStep::Cross) {
        if (pred.open()) {
          ca = pa;
          cb = pb;
        }
      } else if ((c.step == TStep::Delete || c.step == TStep::Change || c.step == TStep::Enter) &&
                 !pred.open()) {
        regions.push_back({pa, ca, pb, cb});
      }
      const int ni = c.pi, nj = c.pj;
      const bool nIn = c.predIn;
      path.push_back({{"k", pk}, {"table", nIn ? "in" : "out"}, {"i", ni}, {"j", nj},
                      {"step", stepName(pred.step)}, {"cost", pred.cost}});
      c = pred;
      k = pk;
      i = ni;
      j = nj;
    }
    reverse(regions.begin(), regions.end());
    reverse(path.begin(), path.end());
  }

  // Verification against the production planner.
  CostOptions altOptions = options;
  altOptions.maxPlans = 4;
  const vector<CostBreakdown> breakdowns =
      VimDiff::calculateBreakdown(initialLines, goalLines, config, altOptions);
  ASSERT_FALSE(breakdowns.empty());
  EXPECT_NEAR(best, breakdowns[0].total, 1e-9);
  ASSERT_EQ(regions.size(), breakdowns[0].regions.size());
  for (size_t r = 0; r < regions.size(); r++) {
    const DiffState& d = breakdowns[0].regions[r].diff;
    EXPECT_EQ(initial.text.substr(regions[r].aBegin, regions[r].aEnd - regions[r].aBegin),
              d.deletedText);
    EXPECT_EQ(goal.text.substr(regions[r].bBegin, regions[r].bEnd - regions[r].bBegin),
              d.insertedText);
  }

  const char* outPath = getenv("VIMFY_TRACE_OUT");
  if (!outPath) {
    cout << "VIMFY_TRACE_OUT unset; verified only (optimum " << best << ", " << regions.size()
         << " region(s))" << endl;
    return;
  }

  json out;
  out["example"] = {{"initial", initial.text}, {"goal", goal.text}};
  out["config"] = "uniform";
  out["insertOverhead"] = typing.entry + typing.esc;
  out["entry"] = typing.entry;
  out["esc"] = typing.esc;
  out["typed"] = json::array();
  out["enterExtra"] = json::array();
  out["changeExtra"] = json::array();
  for (int rj = 0; rj < (int)goal.text.size(); rj++) {
    out["typed"].push_back(typing.PS[rj + 1] - typing.PS[rj]);
    out["enterExtra"].push_back(typing.entry + typing.esc - typing.cut[rj]);
    out["changeExtra"].push_back(typing.esc - typing.cut[rj]);
  }
  out["blocks"] = json::array();
  for (int k = 0; k < (int)blocks.size(); k++)
    out["blocks"].push_back({{"aBegin", blocks[k].aBegin},
                             {"aEnd", blocks[k].aEnd},
                             {"bBegin", blocks[k].bBegin},
                             {"bEnd", blocks[k].bEnd},
                             {"lead", tables[k].lead},
                             {"trail", tables[k].trail}});
  const int N = (int)initial.text.size();
  json delM = json::array(), moveM = json::array();
  for (int a = 0; a <= N; a++) {
    json dRow = json::array(), mRow = json::array();
    for (int e = 0; e <= N; e++) {
      dRow.push_back(e > a ? del.query(a, e) : 0.0);
      mRow.push_back(e > a ? move.query(a, e) : 0.0);
    }
    delM.push_back(dRow);
    moveM.push_back(mRow);
  }
  out["delCost"] = delM;
  out["moveCost"] = moveM;
  out["events"] = tracer.events;
  json cells;
  for (const char* tbl : {"out", "in"}) cells[tbl] = json::array();
  for (int k = 0; k < (int)blocks.size(); k++) {
    for (bool isIn : {false, true}) {
      const TraceGrid& g = isIn ? tables[k].in : tables[k].out;
      json grid = json::array();
      for (const auto& row : g) {
        json jr = json::array();
        for (const TraceCand& c : row)
          jr.push_back(c.cost >= INF
                           ? json(nullptr)
                           : json({{"cost", c.cost}, {"step", stepName(c.step)},
                                   {"pk", c.pk}, {"pi", c.pi}, {"pj", c.pj},
                                   {"ptable", c.predIn ? "in" : "out"}}));
        grid.push_back(jr);
      }
      cells[isIn ? "in" : "out"].push_back(grid);
    }
  }
  out["cells"] = cells;
  out["optPath"] = path;

  // Alternative plans the DP weighed, with raw spans reconstructed from the
  // kept-run identity (b-side begin = prev bEnd + gap on the a side).
  json plans = json::array();
  for (const CostBreakdown& bd : breakdowns) {
    json regs = json::array();
    int prevAEnd = 0, prevBEnd = 0;
    for (const RegionBreakdown& rb : bd.regions) {
      const int aBegin = DiffText::positionToFlatIndex(rb.diff.beginPos, initialLines);
      const int aEnd = aBegin + (int)rb.diff.deletedText.size();
      const int bBegin = prevBEnd + (aBegin - prevAEnd);
      const int bEnd = bBegin + (int)rb.diff.insertedText.size();
      prevAEnd = aEnd;
      prevBEnd = bEnd;
      regs.push_back({{"aBegin", aBegin}, {"aEnd", aEnd}, {"bBegin", bBegin}, {"bEnd", bEnd},
                      {"deleted", rb.diff.deletedText}, {"inserted", rb.diff.insertedText},
                      {"del", rb.del}, {"ins", rb.ins}, {"move", rb.move}});
    }
    plans.push_back({{"cost", bd.total}, {"regions", regs}});
  }
  out["plans"] = plans;

  ofstream f(outPath);
  ASSERT_TRUE(f.is_open()) << outPath;
  f << out.dump(1) << endl;
  cout << "wrote " << outPath << " (optimum " << best << ", " << tracer.events.size()
       << " events)" << endl;
}
