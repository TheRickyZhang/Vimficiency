// tests/Debug/VimDiffTraceExport.cpp
//
// Exports the DP trace of the VimDiff planner for one (initial, goal) example,
// feeding the README dp-walkthrough animation (anim/dp-walkthrough). Dumps
// VimDiff::trace() — the production tables, not a re-run — so it cannot drift
// from the planner; the traced optimum is still asserted against
// calculateBreakdown as a sanity check on the path walk.
//
// Run:
//   VIMFY_TRACE_OUT=anim/dp-walkthrough/trace.json \
//     ./build/tests/vimfy_debug --gtest_filter='VimDiffTraceExport.*'
// Optional: VIMFY_TRACE_INITIAL / VIMFY_TRACE_GOAL override the example
// (literal "\n" for newlines). Without VIMFY_TRACE_OUT the equality check
// still runs; nothing is written.

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/PlannerCosts.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/VimDiffTrace.h"
#include "Types/Lines.h"

using namespace std;
using namespace VimDiff;
using json = nlohmann::json;

namespace {

constexpr const char* STEP_NAMES[] = {"LEADING", "MOVE", "CROSS", "DELETE",
                                      "CHANGE", "ENTER", "TYPE", "EXIT"};

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

json cellJson(const TraceCell& c, int k) {
  if (c.cost >= INF) return nullptr;
  const bool predIn = c.step == TYPE || c.step == EXIT;
  return {{"cost", c.cost}, {"step", STEP_NAMES[c.step]}, {"pk", c.step == CROSS ? k - 1 : k},
          {"pi", c.pi}, {"pj", c.pj}, {"ptable", predIn ? "in" : "out"}};
}

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
  TilingCost del(initial, options.moveDeleteScale, options.maxPrefixCount,
                 TilingCost::Kind::Delete);
  TilingCost move(initial, options.moveDeleteScale, options.maxPrefixCount,
                  TilingCost::Kind::Move);

  const Trace trace = VimDiff::trace(initialLines, goalLines, config, options);
  ASSERT_FALSE(trace.sealedFallback);
  ASSERT_FALSE(trace.path.empty());
  const TracePathCell& top = trace.path.back();
  const double best = trace.blocks[top.block].outAt(top.i, top.j).cost;
  EXPECT_NEAR(best, trace.breakdown.total, 1e-9);

  const char* outPath = getenv("VIMFY_TRACE_OUT");
  if (!outPath) {
    cout << "VIMFY_TRACE_OUT unset; verified only (optimum " << best << ", "
         << trace.breakdown.regions.size() << " region(s))" << endl;
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
    out["enterExtra"].push_back(typing.entry + typing.esc);
    out["changeExtra"].push_back(typing.esc);
  }
  out["blocks"] = json::array();
  for (const TraceBlock& tb : trace.blocks)
    out["blocks"].push_back({{"aBegin", tb.block.aBegin}, {"aEnd", tb.block.aEnd},
                             {"bBegin", tb.block.bBegin}, {"bEnd", tb.block.bEnd},
                             {"lead", tb.lead}, {"trail", tb.trail}});
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
  json cells;
  for (const char* tbl : {"out", "in"}) cells[tbl] = json::array();
  for (int k = 0; k < (int)trace.blocks.size(); k++) {
    const TraceBlock& tb = trace.blocks[k];
    for (bool isIn : {false, true}) {
      json grid = json::array();
      for (int i = 0; i <= tb.block.n(); i++) {
        json row = json::array();
        for (int j = 0; j <= tb.block.m(); j++)
          row.push_back(cellJson(isIn ? tb.inAt(i, j) : tb.outAt(i, j), k));
        grid.push_back(row);
      }
      cells[isIn ? "in" : "out"].push_back(grid);
    }
  }
  out["cells"] = cells;
  json path = json::array();
  for (const TracePathCell& pc : trace.path) {
    const TraceBlock& tb = trace.blocks[pc.block];
    const TraceCell& c = pc.inTable ? tb.inAt(pc.i, pc.j) : tb.outAt(pc.i, pc.j);
    path.push_back({{"k", pc.block}, {"table", pc.inTable ? "in" : "out"}, {"i", pc.i},
                    {"j", pc.j}, {"step", STEP_NAMES[c.step]}, {"cost", c.cost}});
  }
  out["optPath"] = path;

  // The plan with raw spans reconstructed from the kept-run identity (b-side
  // begin = prev bEnd + gap on the a side). A one-element list: the animation
  // scenes that compare alternatives read the committed trace files, which
  // predate the planner's K-best removal.
  json regs = json::array();
  int prevAEnd = 0, prevBEnd = 0;
  for (const RegionBreakdown& rb : trace.breakdown.regions) {
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
  out["plans"] = json::array({{{"cost", trace.breakdown.total}, {"regions", regs}}});

  ofstream f(outPath);
  ASSERT_TRUE(f.is_open()) << outPath;
  f << out.dump(1) << endl;
  cout << "wrote " << outPath << " (optimum " << best << ", " << trace.path.size()
       << " path cells)" << endl;
}
