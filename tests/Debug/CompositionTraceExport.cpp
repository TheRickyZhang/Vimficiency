// tests/Debug/CompositionTraceExport.cpp
//
// Exports a full trace of the composition A* search for one (initial, goal)
// example, feeding the search-walkthrough animation (anim/search-walkthrough).
//
// Runs the production CompositionOptimizer, then rebuilds its search tree from
// the recorded explored-state pop order: at each popped state the per-pop
// transition enumeration from CompositionOptimizer::optimizeImpl is
// transcribed here against the same public context/helpers, so every child
// (edit transitions from precomputed TransformResult buckets, pure-insertion
// strategies, inner NavOptimizer calls) is re-derived with production costs.
// Inner nav calls export their own pop traces; per-edit transform searches
// export theirs (replayed through the interpreter for buffer snapshots).
// Traced results are asserted against the production results so the trace can
// never drift from the production search.
//
// Run:
//   VIMFY_TRACE_OUT=anim/search-walkthrough/trace.json \
//     ./build/tests/vimfy_debug --gtest_filter='CompositionTraceExport.*'
// Optional: VIMFY_TRACE_INITIAL / VIMFY_TRACE_GOAL override the example
// (literal "\n" for newlines). Without VIMFY_TRACE_OUT the equality checks
// still run; nothing is written.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Keyboard/Config.h"
#include "Optimizer/CompositionOptimizer/CompositionMotionSearch.h"
#include "Optimizer/CompositionOptimizer/CompositionNavParams.h"
#include "Optimizer/CompositionOptimizer/CompositionOptimizer.h"
#include "Optimizer/CompositionOptimizer/CompositionSearchContext.h"
#include "Optimizer/CompositionOptimizer/CompositionState.h"
#include "Optimizer/CompositionOptimizer/CompositionStrategies.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Types/CharInterval.h"
#include "Types/Lines.h"
#include "Utils/InterpreterModelReplay.h"
#include "Utils/PrettyText.h"

using namespace std;
using json = nlohmann::json;

namespace {

const char* DEFAULT_INITIAL =
    "int main() {\n  int x = 0;\n  for(int i = 0; i < 10; i++) {\n    int a = bar(i);\n"
    "    int b = baz(a);\n    int c = qux(b);\n    int d = quux(c);\n    int e = corge(d);\n"
    "    x += a + b + c + d + e;\n  }\n}";
const char* DEFAULT_GOAL =
    "int main() {\n  int x = 0;\n  int n = 10;\n  for(int i = 0; i < n; i++) {\n"
    "    int a = bar(i);\n    int b = baz(a);\n    int c = qux(b);\n    int d = quux(c);\n"
    "    int e = corge(d);\n    x -= foo(a + b + c + d + e);\n  }\n}";

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

json linesToJson(const Lines& lines) {
  json arr = json::array();
  for (const string& ln : lines) arr.push_back(ln);
  return arr;
}

json posToJson(const Pos& p) { return json::array({p.line, p.col}); }

json seqToJson(string_view raw) {
  return {{"raw", string(raw)}, {"pretty", VF::prettify(raw)}};
}

// Transcribed from CompositionOptimizer.cpp (anonymous namespace).
CursorPos clampGoalPosToLines(const CursorPos& pos, const Lines& lines) {
  if (lines.empty()) return CursorPos(0, 0);
  int line = clamp(pos.line, 0, lines.lastLine());
  int wantedCol = pos.targetCol >= 0 ? pos.targetCol : pos.col;
  int maxCol = lines[line].empty() ? 0 : static_cast<int>(lines[line].size()) - 1;
  int col = clamp(wantedCol, 0, maxCol);
  return CursorPos(line, col, wantedCol);
}

struct NavCallRec {
  int fromNode;
  string purpose;
  int beginLine;
  Lines subset;
  CursorPos localStart;
  Pos goalFirst, goalLast;
  vector<ExploredState> pops;
  json results = json::array();
  int nodesExplored = 0;
};

struct ChildRec {
  string kind;      // edit | insert | textobj | join | nav | reset0
  string suffix;    // sequence appended to the parent
  int editOffset = 0;
  CompositionState state;
  string status;    // enqueued | improved | dominated | effort-pruned
  bool terminal = false;
  int nodeId = -1;  // filled when this child is later popped
  int navCall = -1;
  double menuCost = -1;  // transform-bucket cost for kind==edit
};

struct NodeRec {
  int id;
  int parent;
  string viaKind;
  string viaSuffix;
  CompositionState state;
  vector<ChildRec> children;
};

}  // namespace

TEST(CompositionTraceExport, ExportExample) {
  const char* initEnv = getenv("VIMFY_TRACE_INITIAL");
  const char* goalEnv = getenv("VIMFY_TRACE_GOAL");
  const Lines initialLines = Lines::unflatten(unescapeNewlines(initEnv ? initEnv : DEFAULT_INITIAL));
  const Lines goalLines = Lines::unflatten(unescapeNewlines(goalEnv ? goalEnv : DEFAULT_GOAL));
  const Config config = Config::uniform();
  const CompositionOptimizerParams params;
  const CursorPos initialPos(0, 0);
  const NavBoundary boundary;
  const NavContext navContext;

  // The final cursor target is where the last edit leaves the cursor; a
  // throwaway context provides it before the real one is built around it.
  CursorPos goalPos(0, 0);
  {
    CompositionSearchContext probe(initialLines, initialPos, goalLines, CursorPos(0, 0),
                                   "", navContext, boundary, params, config);
    ASSERT_GT(probe.totalEdits(), 0);
    goalPos = probe.edits.back().transformResult.getGoalPos();
  }

  CompositionSearchContext ctx(initialLines, initialPos, goalLines, goalPos, "",
                               navContext, boundary, params, config);
  const int totalEdits = ctx.totalEdits();

  CompositionOptimizer opt(config);
  CompositionResult res = opt.optimize(initialLines, initialPos, goalLines, goalPos, params);

  cout << fixed << setprecision(2);
  cout << "production: " << res.getStats() << " goalPos=" << goalPos << "\n";
  for (size_t i = 0; i < res.getResults().size(); i++) {
    cout << "  [" << i << "] " << VF::prettify(res.getResults()[i].getSequence().view())
         << "  " << res.getResults()[i].getCost() << "\n";
  }

  // ===========================================================================
  // Re-derive the search tree in recorded pop order.
  // ===========================================================================
  NavOptimizer navOptimizer(config);
  auto scoreState = [&](const CompositionState& state) {
    return ctx.heuristic(state, state.getEditsCompleted());
  };
  CompositionStateFactory states(config, scoreState);

  vector<NodeRec> nodes;
  vector<NavCallRec> navCalls;
  unordered_map<CompositionStateKey, double, CompositionStateKeyHash> costMap;

  auto classify = [&](ChildRec& c) {
    if (c.state.getEffort() > ctx.maxEffort) {
      c.status = "effort-pruned";
      return;
    }
    c.terminal = (c.state.getEditsCompleted() == totalEdits &&
                  c.state.getPos() == goalPos);
    auto it = costMap.find(c.state.getKey());
    if (it == costMap.end()) {
      if (!c.terminal) costMap.emplace(c.state.getKey(), c.state.getCost());
      c.status = "enqueued";
    } else if (c.state.getCost() <= it->second) {
      it->second = c.state.getCost();
      c.status = "improved";
    } else {
      c.status = "dominated";
    }
  };

  // Transcribed from optimizeImpl's exploreMotionsToInterval + sliceMotionSubset.
  auto exploreMotions = [&](NodeRec& node, CursorPos pos,
                            int targetBeginLine, int targetEndLine,
                            const Lines& fromLines, int maxResults,
                            bool keepMultiplePerLanding, auto&& makeLocalInterval,
                            const string& purpose) {
    auto [beginLine, endLine] = fromLines.minmaxBoundWithPadding(
        min(pos.line, targetBeginLine), max(pos.line + 1, targetEndLine + 1),
        params.navPaddingAbove, params.navPaddingBelow);
    Lines subset = fromLines.getLineRange(beginLine, endLine);
    CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
    CursorPos subsetFirst(0, 0);
    CursorPos subsetEnd(static_cast<int>(subset.size()) - 1, subset.back().effectiveSize());
    NavBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
        beginLine > 0 || boundary.hasLinesAbove(),
        endLine <= fromLines.lastLine() || boundary.hasLinesBelow());
    auto window = buildCompositionBufferIndexWindow(fromLines, beginLine, endLine);
    CharInterval localInterval = makeLocalInterval(subset, beginLine);

    auto navParams = navParamsForCompositionMotion(params)
        .withMaxResults(maxResults)
        .withMaxResultsPerEndPos(keepMultiplePerLanding ? 2 : 1);
    auto navResult = navOptimizer.optimize(
        subset, localPos, localInterval, navParams, "", subsetBoundary,
        navContext, window.index, window.lineOffset, nullptr);

    NavCallRec rec{node.id, purpose, beginLine, subset, localPos,
                   localInterval.first, localInterval.last,
                   navResult.getStats().exploredStates()};
    rec.nodesExplored = navResult.getStats().nodesExplored();
    for (const LandingResult& mr : navResult.getResults()) {
      rec.results.push_back({{"seq", seqToJson(mr.getSequence().view())},
                             {"cost", mr.getCost()},
                             {"landing", posToJson(mr.getGoalPos())}});
    }
    int navCallId = static_cast<int>(navCalls.size());
    navCalls.push_back(std::move(rec));

    for (const LandingResult& mr : navResult.getResults()) {
      if (mr.getSequence().empty()) continue;
      CursorPos landing = mr.getGoalPos();
      landing.line += beginLine;
      ChildRec c{"nav", mr.getSequence().str(), 0,
                 states.afterNavResult(node.state, mr.getSequence(), landing)};
      c.navCall = navCallId;
      classify(c);
      node.children.push_back(std::move(c));
    }
  };

  // Transcribed from optimizeImpl's per-pop transition enumeration.
  auto enumerateChildren = [&](NodeRec& node) {
    const CompositionState& s = node.state;
    CursorPos pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    if (editsCompleted == totalEdits) {
      exploreMotions(node, pos, goalPos.line, goalPos.line,
          ctx.getLinesAfter(editsCompleted),
          clamp(params.maxResults, 1, 10), true,
          [&](const Lines&, int beginLine) -> CharInterval {
            CursorPos localGoal(goalPos.line - beginLine, goalPos.col, goalPos.targetCol);
            return CharInterval(localGoal, localGoal);
          },
          "post-final");
      return;
    }

    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
    const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

    if (nextEdit.isPureInsertion()) {
      auto dispatchInsertion = [&](const CompositionStrategies::Insertion& ins) {
        const bool inRange = (pos.line == ins.targetLine &&
                              pos.col >= ins.beginCol && pos.col < ins.endCol);
        if (inRange) {
          ChildRec c{"insert", ins.insertCmd, 0,
                     states.afterEditTransition(s, Sequence(ins.insertCmd),
                                                ins.goalPos, Mode::Normal)};
          classify(c);
          node.children.push_back(std::move(c));
          return;
        }
        auto search = buildCompositionRangeMotionSearch(
            currentLines, pos, ins.targetLine, ins.beginCol, ins.endCol,
            boundary, params);
        if (!search) return;
        auto navResult = optimizeCompositionRangeMotion(
            navOptimizer, *search, navContext, params, 1, nullptr);

        NavCallRec rec{node.id, "insert:" + ins.structural, search->beginLine,
                       search->subset, search->localCursor,
                       search->motionRange.first, search->motionRange.last,
                       navResult.getStats().exploredStates()};
        rec.nodesExplored = navResult.getStats().nodesExplored();
        for (const LandingResult& mr : navResult.getResults()) {
          rec.results.push_back({{"seq", seqToJson(mr.getSequence().view())},
                                 {"cost", mr.getCost()},
                                 {"landing", posToJson(mr.getGoalPos())}});
        }
        int navCallId = static_cast<int>(navCalls.size());
        navCalls.push_back(std::move(rec));

        for (const LandingResult& mr : navResult.getResults()) {
          if (mr.getSequence().empty()) continue;
          Sequence fullSeq = mr.getSequence();
          const int motionBytes = static_cast<int>(fullSeq.size());
          fullSeq.append(ins.insertCmd);
          ChildRec c{"insert", fullSeq.str(), motionBytes,
                     states.afterEditTransition(s, fullSeq, ins.goalPos, Mode::Normal)};
          c.navCall = navCallId;
          classify(c);
          node.children.push_back(std::move(c));
        }
      };
      CompositionStrategies::enumerateInsertions(nextEdit, currentLines, dispatchInsertion);
      return;
    }

    const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
    span<const Result> editAlternatives = transformResult.resultsAt(pos.line, pos.col);
    bool canUseTransformAlternatives = true;
    if (!editAlternatives.empty() && pos.targetCol != pos.col) {
      canUseTransformAlternatives = false;
      if (pos.col == 0) {
        ChildRec c{"reset0", "0", 0,
                   states.afterNavResult(s, Sequence("0"), CursorPos(pos.line, 0))};
        classify(c);
        node.children.push_back(std::move(c));
      }
    }

    if (canUseTransformAlternatives) {
      for (size_t resultIndex = 0; resultIndex < editAlternatives.size(); resultIndex++) {
        const Result& r = editAlternatives[resultIndex];
        CursorPos editGoalPos = transformResult.goalPosAt(pos.line, pos.col, resultIndex);
        if (transformResult.hasResultGoals()) {
          editGoalPos = clampGoalPosToLines(editGoalPos, ctx.getLinesAfter(editsCompleted + 1));
        }
        ChildRec c{"edit", r.getSequence().str(), 0,
                   states.afterEditTransition(s, r.getSequence(), editGoalPos, Mode::Normal)};
        c.menuCost = r.getCost();
        classify(c);
        node.children.push_back(std::move(c));
      }
    }

    const BracketQuoteContext& bqContext = ctx.edits[editsCompleted].bracketQuoteContext;
    if (bqContext.line == pos.line) {
      CompositionStrategies::enumerateBracketQuotes(
          nextEdit, currentLines, bqContext,
          [&](const CompositionStrategies::BracketQuote& bq) {
            if (bq.line != pos.line || bq.col != pos.col) return;
            ChildRec c{"textobj", bq.body, 0,
                       states.afterEditTransition(s, Sequence(bq.body),
                                                  bq.goalPos, Mode::Normal)};
            classify(c);
            node.children.push_back(std::move(c));
          });
    }

    const auto& joinPlan = ctx.edits[editsCompleted].joinPlan;
    if (joinPlan && pos.line == joinPlan->entryLine) {
      ChildRec c{"join", joinPlan->sequence.str(), 0,
                 states.afterEditTransition(s, joinPlan->sequence,
                                            joinPlan->goalPos, Mode::Normal)};
      classify(c);
      node.children.push_back(std::move(c));
    }

    if (editAlternatives.empty()) {
      if (joinPlan && pos.line != joinPlan->entryLine) {
        int entryLine = joinPlan->entryLine;
        exploreMotions(node, pos, entryLine, entryLine, currentLines, 1, false,
            [&](const Lines& subset, int beginLine) -> CharInterval {
              int localLine = entryLine - beginLine;
              return CharInterval(CursorPos(localLine, 0),
                                  CursorPos(localLine, subset[localLine].lastCol()));
            },
            "join-entry");
      }

      exploreMotions(node, pos, nextEdit.beginPos.line, nextEdit.editEndLine() - 1,
          currentLines, nextEdit.origCharCount(), false,
          [&](const Lines& subset, int beginLine) -> CharInterval {
            CursorPos localBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
            CursorPos localEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);
            return CharInterval(CharRange(localBegin, localEnd), subset);
          },
          "inter-edit");
    }
  };

  // Walk the recorded pops: node 0 is the seed; each later pop must match a
  // previously enumerated child.
  const auto& explored = res.getExploredStates();
  ASSERT_FALSE(explored.empty());
  EXPECT_TRUE(explored[0].sequence.empty());
  EXPECT_EQ(explored[0].line, initialPos.line);
  EXPECT_EQ(explored[0].col, initialPos.col);

  {
    NodeRec root{0, -1, "seed", "", states.initial(initialPos)};
    costMap[root.state.getKey()] = root.state.getCost();
    enumerateChildren(root);
    nodes.push_back(std::move(root));
  }

  for (size_t k = 1; k < explored.size(); k++) {
    const CompositionExploredState& ex = explored[k];
    NodeRec* made = nullptr;
    for (NodeRec& n : nodes) {
      for (ChildRec& c : n.children) {
        if (c.nodeId >= 0 || c.status == "effort-pruned" || c.status == "dominated")
          continue;
        if (c.state.getSequence().str() != ex.sequence) continue;
        c.nodeId = static_cast<int>(nodes.size());
        NodeRec child{c.nodeId, n.id, c.kind, c.suffix, c.state};
        EXPECT_EQ(child.state.getPos().line, ex.line) << "pop " << k;
        EXPECT_EQ(child.state.getPos().col, ex.col) << "pop " << k;
        EXPECT_EQ(child.state.getEditsCompleted(), ex.editsCompleted) << "pop " << k;
        EXPECT_NEAR(child.state.getEffort(), ex.effort, 1e-9) << "pop " << k;
        nodes.push_back(std::move(child));
        made = &nodes.back();
        break;
      }
      if (made) break;
    }
    ASSERT_NE(made, nullptr) << "pop " << k << " '" << ex.sequence
                             << "' has no matching enumerated child";
    enumerateChildren(*made);
  }

  // Every production result must appear among enqueued terminal children.
  int terminalCount = 0;
  for (const NodeRec& n : nodes)
    for (const ChildRec& c : n.children)
      if (c.terminal && (c.status == "enqueued" || c.status == "improved")) terminalCount++;
  for (const Result& r : res.getResults()) {
    bool found = false;
    for (NodeRec& n : nodes) {
      for (ChildRec& c : n.children) {
        if (!c.terminal || c.state.getSequence().str() != r.getSequence().str()) continue;
        EXPECT_NEAR(c.state.getEffort(), r.getCost(), 1e-9);
        found = true;
      }
    }
    EXPECT_TRUE(found) << "production result not re-derived: "
                       << VF::prettify(r.getSequence().view());
  }
  EXPECT_EQ(res.getStats().totalPops(),
            static_cast<int>(explored.size()) + static_cast<int>(res.getResults().size()));
  cout << "re-derived " << nodes.size() << " nodes, " << navCalls.size()
       << " nav calls, " << terminalCount << " terminal candidates\n";

  // ===========================================================================
  // Winning path segments, and the closest pruned alternatives. A dominated
  // child shares its state key with a node on the winning path, so the
  // winner's remaining transitions complete it verbatim; that gives the full
  // sequence and cost the search would have produced for that branch. Each
  // completed sequence is replay-verified through the interpreter.
  // ===========================================================================
  json resultSegments = json::array();
  json alternatives = json::array();
  {
    ASSERT_FALSE(res.getResults().empty());
    const Result& top = res.getResults()[0];
    const NodeRec* termParent = nullptr;
    const ChildRec* termChild = nullptr;
    for (const NodeRec& n : nodes)
      for (const ChildRec& c : n.children)
        if (c.terminal && c.state.getSequence().str() == top.getSequence().str()) {
          termParent = &n;
          termChild = &c;
        }
    ASSERT_NE(termChild, nullptr);

    auto viaSegments = [&](int nodeId) {
      vector<string> segs;
      vector<int> chain;
      for (int id = nodeId; id != -1; id = nodes[id].parent) chain.push_back(id);
      for (size_t i = chain.size() - 1; i >= 1; i--)
        segs.push_back(nodes[chain[i - 1]].viaSuffix);
      return segs;
    };

    auto replayToGoal = [&](const string& seq) {
      InterpreterReplayResult r = applyUserSequence(initialLines, initialPos, seq);
      EXPECT_EQ(linesToJson(r.lines), linesToJson(goalLines))
          << "candidate does not replay to the goal buffer: " << VF::prettify(seq);
      EXPECT_EQ(r.cursor.line, goalPos.line) << VF::prettify(seq);
      EXPECT_EQ(r.cursor.col, goalPos.col) << VF::prettify(seq);
    };

    vector<int> path;  // node ids root..termParent, in order
    for (int id = termParent->id; id != -1; id = nodes[id].parent) path.push_back(id);
    reverse(path.begin(), path.end());

    for (const string& s : viaSegments(termParent->id)) resultSegments.push_back(seqToJson(s));
    resultSegments.push_back(seqToJson(termChild->suffix));
    replayToGoal(top.getSequence().str());

    vector<json> alts;
    for (const NodeRec& n : nodes) {
      for (const ChildRec& c : n.children) {
        if (c.status != "dominated") continue;
        for (size_t pi = 0; pi < path.size(); pi++) {
          const NodeRec& p = nodes[path[pi]];
          if (!(p.state.getKey() == c.state.getKey())) continue;
          double fullCost = c.state.getEffort() + (top.getCost() - p.state.getEffort());
          json segs = json::array();
          vector<string> prefix = viaSegments(n.id);
          for (const string& s : prefix) segs.push_back(seqToJson(s));
          int branchIndex = static_cast<int>(prefix.size());
          segs.push_back(seqToJson(c.suffix));
          string fullSeq = c.state.getSequence().str();
          for (size_t qi = pi + 1; qi < path.size(); qi++) {
            segs.push_back(seqToJson(nodes[path[qi]].viaSuffix));
            fullSeq += nodes[path[qi]].viaSuffix;
          }
          segs.push_back(seqToJson(termChild->suffix));
          fullSeq += termChild->suffix;
          replayToGoal(fullSeq);
          alts.push_back({{"cost", fullCost},
                          {"seq", seqToJson(fullSeq)},
                          {"segments", std::move(segs)},
                          {"branchIndex", branchIndex},
                          {"branchNode", n.id},
                          {"branchSuffix", seqToJson(c.suffix)},
                          {"mergeNode", p.id},
                          {"kind", c.kind}});
          break;
        }
      }
    }
    sort(alts.begin(), alts.end(), [](const json& a, const json& b) {
      return a["cost"].get<double>() < b["cost"].get<double>();
    });
    for (json& a : alts) alternatives.push_back(std::move(a));
    cout << "alternatives: " << alternatives.size() << "\n";
    for (const json& a : alternatives)
      cout << "  " << a["cost"].get<double>() << "  "
           << a["seq"]["pretty"].get<string>() << "\n";
  }

  // ===========================================================================
  // Per-edit transform search traces, with interpreter-replayed buffers.
  // ===========================================================================
  json transformZooms = json::array();
  for (int i = 0; i < totalEdits; i++) {
    const DiffState& d = ctx.edits[i].diffState;
    if (d.isPureInsertion()) continue;
    const TransformResult& tr = ctx.edits[i].transformResult;
    const auto& pops = tr.getStats().exploredStates();
    Lines effLines = d.boundary.withBoundary(d.deletedLines());
    json goalEffJson = linesToJson(d.boundary.withBoundary(d.insertedLines()));

    vector<CursorPos> seeds;
    for (const ExploredState& p : pops)
      if (p.sequence.empty()) seeds.emplace_back(p.line, p.col);

    json popArr = json::array();
    for (const ExploredState& p : pops) {
      json entry = {{"pos", json::array({p.line, p.col})},
                    {"g", p.effort},
                    {"seq", seqToJson(p.sequence)}};
      for (const CursorPos& seed : seeds) {
        InterpreterReplayResult replay = applyUserSequence(effLines, seed, p.sequence);
        json replayLines = linesToJson(replay.lines);
        // Goal-converted states keep the pre-conversion cursor in the trace;
        // for those, the replayed buffer matching the goal identifies the seed.
        const bool cursorMatch =
            replay.cursor.line == p.line && replay.cursor.col == p.col;
        const bool reachedGoal = replayLines == goalEffJson;
        if (cursorMatch || reachedGoal) {
          entry["start"] = posToJson(seed);
          entry["lines"] = std::move(replayLines);
          entry["cursor"] = posToJson(replay.cursor);
          entry["mode"] = replay.mode == Mode::Insert ? "i" : "n";
          entry["reachedGoal"] = reachedGoal;
          break;
        }
      }
      EXPECT_TRUE(entry.contains("start"))
          << "transform pop replay failed for edit " << i << " '" << p.sequence << "'";
      popArr.push_back(std::move(entry));
    }

    json starts = json::array();
    for (const CursorPos& sp : tr.startPositions()) {
      json bucket = json::array();
      auto span = tr.resultsAt(sp.line, sp.col);
      for (size_t r = 0; r < span.size(); r++) {
        bucket.push_back({{"seq", seqToJson(span[r].getSequence().view())},
                          {"cost", span[r].getCost()},
                          {"goal", posToJson(tr.goalPosAt(sp.line, sp.col, r))}});
      }
      starts.push_back({{"pos", posToJson(sp)}, {"results", std::move(bucket)}});
    }

    transformZooms.push_back({
        {"editIdx", i},
        {"effectiveLines", linesToJson(effLines)},
        {"leftColOffset", d.boundary.leftColOffset()},
        {"rightColOffset", d.boundary.rightColOffset()},
        {"goalPos", posToJson(tr.getGoalPos())},
        {"pops", std::move(popArr)},
        {"starts", std::move(starts)},
        {"stats", {{"nodes", tr.getStats().nodesExplored()},
                   {"pops", tr.getStats().totalPops()}}},
    });
  }

  // ===========================================================================
  // Suffix edit costs (same algorithm as computeSuffixEditCosts).
  // ===========================================================================
  vector<double> suffixCosts(totalEdits + 1, 0.0);
  for (int i = totalEdits - 1; i >= 0; i--) {
    vector<double> costs;
    for (const auto& bucket : ctx.edits[i].transformResult.getResults())
      if (!bucket.empty()) costs.push_back(bucket[0].getCost());
    if (ctx.edits[i].joinPlan) costs.push_back(ctx.edits[i].joinPlan->effort);
    double medianCost = 100.0;
    if (!costs.empty()) {
      size_t mid = costs.size() / 2;
      nth_element(costs.begin(), costs.begin() + mid, costs.end());
      medianCost = costs[mid];
    }
    suffixCosts[i] = suffixCosts[i + 1] + medianCost;
  }

  // ===========================================================================
  // JSON dump.
  // ===========================================================================
  json out;
  out["initial"] = linesToJson(initialLines);
  out["goal"] = linesToJson(goalLines);
  out["initialPos"] = posToJson(initialPos);
  out["goalPos"] = posToJson(goalPos);
  out["weights"] = {{"effort", ctx.effortWeight},
                    {"distance", ctx.distanceWeight},
                    {"overshootPenalty", ctx.overshootPenalty}};
  out["suffixEditCosts"] = suffixCosts;

  json planArr = json::array();
  json fenceposts = json::array();
  fenceposts.push_back(linesToJson(ctx.getLinesAfter(0)));
  for (int i = 0; i < totalEdits; i++) {
    const DiffState& d = ctx.edits[i].diffState;
    json entry = {
        {"idx", i},
        {"begin", posToJson(d.beginPos)},
        {"end", posToJson(d.endPos)},
        {"del", d.deletedText},
        {"ins", d.insertedText},
        {"pureInsertion", d.isPureInsertion()},
    };
    if (ctx.edits[i].joinPlan) {
      entry["joinPlan"] = {{"seq", seqToJson(ctx.edits[i].joinPlan->sequence.view())},
                           {"effort", ctx.edits[i].joinPlan->effort},
                           {"entryLine", ctx.edits[i].joinPlan->entryLine}};
    }
    planArr.push_back(std::move(entry));
    fenceposts.push_back(linesToJson(ctx.getLinesAfter(i + 1)));
  }
  out["plan"] = std::move(planArr);
  out["fenceposts"] = std::move(fenceposts);

  json nodeArr = json::array();
  for (const NodeRec& n : nodes) {
    json children = json::array();
    for (const ChildRec& c : n.children) {
      json cj = {{"kind", c.kind},
                 {"suffix", seqToJson(c.suffix)},
                 {"pos", posToJson(c.state.getPos())},
                 {"e", c.state.getEditsCompleted()},
                 {"g", c.state.getEffort()},
                 {"f", c.state.getCost()},
                 {"status", c.status},
                 {"terminal", c.terminal}};
      if (c.nodeId >= 0) cj["nodeId"] = c.nodeId;
      if (c.navCall >= 0) cj["navCall"] = c.navCall;
      if (c.editOffset > 0) cj["editOffset"] = c.editOffset;
      if (c.menuCost >= 0) cj["menuCost"] = c.menuCost;
      children.push_back(std::move(cj));
    }
    nodeArr.push_back({{"id", n.id},
                       {"parent", n.parent},
                       {"viaKind", n.viaKind},
                       {"viaSuffix", seqToJson(n.viaSuffix)},
                       {"e", n.state.getEditsCompleted()},
                       {"pos", posToJson(n.state.getPos())},
                       {"g", n.state.getEffort()},
                       {"f", n.state.getCost()},
                       {"seq", seqToJson(n.state.getSequence().view())},
                       {"children", std::move(children)}});
  }
  out["nodes"] = std::move(nodeArr);

  json navArr = json::array();
  for (const NavCallRec& nc : navCalls) {
    json popArr = json::array();
    for (const ExploredState& p : nc.pops) {
      popArr.push_back({{"pos", json::array({p.line, p.col})},
                        {"g", p.effort},
                        {"seq", seqToJson(p.sequence)}});
    }
    navArr.push_back({{"fromNode", nc.fromNode},
                      {"purpose", nc.purpose},
                      {"beginLine", nc.beginLine},
                      {"lines", linesToJson(nc.subset)},
                      {"start", posToJson(nc.localStart)},
                      {"goalFirst", posToJson(nc.goalFirst)},
                      {"goalLast", posToJson(nc.goalLast)},
                      {"pops", std::move(popArr)},
                      {"results", nc.results},
                      {"nodes", nc.nodesExplored}});
  }
  out["navCalls"] = std::move(navArr);

  json resultArr = json::array();
  for (const Result& r : res.getResults()) {
    resultArr.push_back({{"seq", seqToJson(r.getSequence().view())},
                         {"cost", r.getCost()}});
  }
  out["results"] = std::move(resultArr);
  out["resultSegments"] = std::move(resultSegments);
  out["alternatives"] = std::move(alternatives);
  out["stats"] = {{"pops", res.getStats().totalPops()},
                  {"nodes", res.getStats().nodesExplored()},
                  {"navNodes", res.getStats().navNodesExplored()},
                  {"editNodes", res.getStats().editNodesExplored()}};
  out["transformZooms"] = std::move(transformZooms);

  if (const char* outPath = getenv("VIMFY_TRACE_OUT")) {
    ofstream f(outPath);
    ASSERT_TRUE(f.good()) << "cannot open " << outPath;
    f << out.dump(1);
    cout << "trace written to " << outPath << "\n";
  }
}
