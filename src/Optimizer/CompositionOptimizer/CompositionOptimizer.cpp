#include "CompositionOptimizer.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "CompositionMotionSearch.h"
#include "CompositionSearchContext.h"
#include "CompositionNavParams.h"
#include "CompositionStrategies.h"
#include "EditSequenceSpan.h"
#include "Optimizer/BuildTypedCommands.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"

#include "Effort/RunningEffort.h"
#include "Optimizer/CompositionOptimizer/CompositionState.h"
#include "Utils/Debug.h"
#include "Utils/PrettyText.h"

using namespace std;

namespace {

#if defined(__GNUC__) || defined(__clang__)
#define VF_NO_UNIQUE_ADDRESS [[no_unique_address]]
#else
#define VF_NO_UNIQUE_ADDRESS
#endif

// =============================================================================
// Edit-tracer policies (display-only segmentation, off the hot path)
// =============================================================================
//
// The composition A* search can optionally record one byte span per planned
// edit, so Explore can render Optimal-N header rows segmented by the plan
// rather than by Vim-token kind. Tracing is opt-in per call:
//
//   optimize()              -> NoTrace        (zero per-state cost)
//   optimizeWithEditSpans() -> EditSpanTrace  (one uint32 per state +
//                                              arena allocation per edit
//                                              transition only)
//
// Both policies model the same four lifecycle events and the same `State`
// associated type. The contract is the `EditTracer` concept below; every
// `optimizeImpl` site that touches the tracer goes through one of these
// named methods, never through `if constexpr (Trace::collecting)` — with one
// load-bearing exception at the goal-finalize site, where `collecting`
// decides whether to push to `resultSpans` at all.
//
//   onSearchStart() -> State
//       Initial trace state attached to the seed CompositionState.
//
//   onEditTransition(parent, editIndex, begin, end) -> State
//       Called when the search enqueues a CompositionState produced by
//       applying planned edit #editIndex. (begin, end) is the half-open byte
//       span this transition contributed to the result's flat sequence.
//
//   onMotionTransition(parent) -> State
//       Called when the search enqueues a CompositionState produced by pure
//       motion (no planned edit advanced). Trace state passes through
//       unchanged in both current policies, but the explicit call site keeps
//       motion vs edit transitions symmetric.
//
//   onGoalReached(state, totalEdits) -> std::vector<EditSequenceSpan>
//       Called at goal acceptance to reconstruct the per-edit byte spans for
//       the result whose tail state is `state`.
//
// `collecting` is a static bool — true iff the tracer is doing real work.
// Wrap-only-when-needed via [[no_unique_address]] inside QueueEntry: with
// NoTrace, sizeof(QueueEntry) == sizeof(CompositionState) and the queue
// behaves identically to a plain CompositionState pq.

struct EditTraceNode {
  uint32_t previous = 0;     // 0 = root sentinel (never a real node)
  uint8_t editIndex = 0;
  uint32_t beginByte = 0;
  uint32_t endByte = 0;
};

struct NoTrace {
  struct State {};
  static constexpr bool collecting = false;

  // All event methods are empty — they compile away under -O.
  State onSearchStart() const { return {}; }
  State onEditTransition(const State&, int /*editIndex*/,
                         uint32_t /*begin*/, uint32_t /*end*/) const {
    return {};
  }
  State onMotionTransition(const State&) const { return {}; }
  vector<EditSequenceSpan> onGoalReached(const State&,
                                         int /*totalEdits*/) const {
    return {};
  }
};

struct EditSpanTrace {
  struct State { uint32_t node = 0; };
  static constexpr bool collecting = true;

  // Index 0 is a sentinel root used as "no parent". Real nodes start at 1.
  vector<EditTraceNode> arena{EditTraceNode{}};

  State onSearchStart() const { return {}; }
  State onMotionTransition(const State& parent) const { return parent; }

  State onEditTransition(const State& parent, int editIndex,
                         uint32_t beginByte, uint32_t endByte) {
    assert(parent.node < arena.size() && "trace parent out of range");
    assert(editIndex >= 0 && editIndex < 256 && "editIndex must fit in uint8_t");
    arena.push_back(EditTraceNode{
        .previous = parent.node,
        .editIndex = static_cast<uint8_t>(editIndex),
        .beginByte = beginByte,
        .endByte = endByte,
    });
    return State{static_cast<uint32_t>(arena.size() - 1)};
  }

  vector<EditSequenceSpan> onGoalReached(const State& s, int totalEdits) const {
    vector<EditSequenceSpan> spans(totalEdits);
    vector<bool> seen(totalEdits, false);
    int count = 0;
    uint32_t node = s.node;
    while (node != 0) {
      assert(node < arena.size() && "trace chain out of range");
      const EditTraceNode& n = arena[node];
      assert(n.editIndex < totalEdits && "trace editIndex past totalEdits");
      assert(!seen[n.editIndex] && "trace recorded duplicate editIndex");
      assert(n.beginByte <= n.endByte && "trace span byte range is inverted");
      seen[n.editIndex] = true;
      spans[n.editIndex] = EditSequenceSpan{n.beginByte, n.endByte};
      ++count;
      node = n.previous;
    }
    assert(count == totalEdits && "trace chain must include exactly one node per edit");
    return spans;
  }
};

// Contract for both tracer policies; checked at the optimizeImpl template
// head so substitution failures surface at the call site, not deep in the
// function body. Mirrors the `GoalHandlerCore` concept shape in
// `TransformTransitionDispatcher.h`.
template<class T>
concept EditTracer = requires(
    T t, const T ct, const typename T::State s,
    int editIndex, uint32_t begin, uint32_t end, int totalEdits) {
  typename T::State;
  { T::collecting } -> std::convertible_to<bool>;
  { ct.onSearchStart() } -> std::same_as<typename T::State>;
  { t.onEditTransition(s, editIndex, begin, end) } -> std::same_as<typename T::State>;
  { ct.onMotionTransition(s) } -> std::same_as<typename T::State>;
  { ct.onGoalReached(s, totalEdits) } -> std::same_as<vector<EditSequenceSpan>>;
};

template <EditTracer Trace>
struct QueueEntry {
  CompositionState state;
  VF_NO_UNIQUE_ADDRESS typename Trace::State trace;

  // Min-heap by composition cost; trace state is invisible to ordering.
  bool operator>(const QueueEntry& other) const {
    return state > other.state;
  }
};

// Clamp cursor to a valid normal-mode position on concrete post-edit lines.
// Uses targetCol when available so vertical sticky-column intent is preserved.
CursorPos clampGoalPosToLines(const CursorPos& pos, const Lines& lines) {
  if (lines.empty()) return CursorPos(0, 0);

  int line = clamp(pos.line, 0, lines.lastLine());
  int wantedCol = pos.targetCol >= 0 ? pos.targetCol : pos.col;
  int maxCol = lines[line].empty() ? 0 : static_cast<int>(lines[line].size()) - 1;
  int col = clamp(wantedCol, 0, maxCol);
  return CursorPos(line, col, wantedCol);
}

std::optional<Result> buildWholeBufferRewriteFallback(
    const Config& config, const Lines& initialLines, const Lines& goalLines,
    CursorPos goalPos, const NavContext& navigationContext) {
  Sequence seq("gg0VGc");
  KeyedSequence typed;
  if (!leadingWhitespace(initialLines.front()).empty()) {
    typed += KeyedSequence::CtrlU;
  }
  typed += buildTypedCommands(goalLines);
  seq.append(typed.seq.view());

  CursorPos exitPos = typedCommandsExitCursor(CursorPos(0, 0), goalLines);
  if (exitPos != goalPos) {
    NavOptimizer nav(config);
    LandingNavResult navResult = nav.optimize(
        goalLines, exitPos, goalPos,
        NavOptimizerParams{}.withMaxResults(1), "", NavBoundary(),
        navigationContext);
    if (navResult.getResults().empty()) return std::nullopt;
    seq.append(navResult.getResults()[0].getSequence().view());
  }

  return Result(seq, getEffort(seq.view(), config));
}

// Templated search core. Both public optimize entrypoints route through here.
// `Trace` satisfies the `EditTracer` concept above. Today: `NoTrace` (zero
// per-state cost, used by `optimize()`) or `EditSpanTrace` (used by
// `optimizeWithEditSpans()`); under `NoTrace` the queue entries are byte-
// equivalent to a plain CompositionState pq.
//
// Returns the goal results plus, when `Trace::collecting`, a parallel vector
// of per-result edit spans. With NoTrace the spans vector is empty. All
// tracer interaction goes through `trace.onSearchStart() / onEditTransition /
// onMotionTransition / onGoalReached`; the only `if constexpr (Trace::collecting)`
// left here gates pushing to `resultSpans` at the goal site (see comment there).
template <EditTracer Trace>
static std::pair<vector<Result>, vector<vector<EditSequenceSpan>>>
optimizeImpl(
    const Config& config,
    const Lines& initialLines, const CursorPos initialPos,
    const Lines& goalLines, const CursorPos goalPos,
    const CompositionOptimizerParams& params, std::string_view userSequence,
    const NavBoundary& boundary, const NavContext& navigationContext,
    CompositionSearchContext& ctx,
    Trace& trace) {
  NavOptimizer navOptimizer(config);

  vector<Result> results;
  vector<vector<EditSequenceSpan>> resultSpans;

  using Entry = QueueEntry<Trace>;
  std::priority_queue<Entry, vector<Entry>, std::greater<Entry>> pq;

  auto scoreState = [&](const CompositionState& state) {
    return ctx.heuristic(state, state.getEditsCompleted());
  };
  CompositionStateFactory states(config, scoreState);

  CompositionState startingState = states.initial(initialPos);
  pq.push(Entry{startingState, trace.onSearchStart()});
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  auto enqueueState = [&](CompositionState&& newState,
                          typename Trace::State traceState) {
    if (newState.getEffort() > ctx.maxEffort) {
      debug("  pruned (effort", newState.getEffort(), ">", ctx.maxEffort, "):",
            "\"" + newState.getSequence().str() + "\"");
      return;
    }

    double newCost = newState.getCost();
    const CompositionStateKey newKey = newState.getKey();
    // Terminal states (all edits done AND cursor at goalPos) are not cached so
    // multiple distinct paths to goal can each emit a result. Post-final-edit
    // nav states (all edits done but cursor still moving toward goalPos) ARE
    // cached, same as inter-edit nav, to avoid redundant exploration.
    const bool isTerminal = (newState.getEditsCompleted() == ctx.totalEdits() &&
                             newState.getPos() == ctx.goalPos);
    auto it = ctx.costMap.find(newKey);

    if (it == ctx.costMap.end()) {
      if (!isTerminal) {
        ctx.costMap.emplace(newKey, newCost);
      }
      pq.push(Entry{std::move(newState), traceState});
    } else if (newCost <= it->second) {
      it->second = newCost;
      pq.push(Entry{std::move(newState), traceState});
    } else {
      debug("  not enqueued (cost", newCost, ">=", it->second, "):",
            "\"" + newState.getSequence().str() + "\"");
    }
  };
  // editOffset splits a transition sequence whose prefix is motion (Navigate)
  // and suffix is the planned edit. Used for pure-insertion strategies that
  // fuse motion + insertCmd into one transition; passing editOffset > 0 makes
  // the recorded edit span cover only the insertion suffix.
  auto enqueueEditTransition = [&](const Entry& parent,
                                   const Sequence& editSequence,
                                   const CursorPos& goalPos,
                                   int editsAfter,
                                   uint32_t editOffset = 0) {
    const uint32_t base = static_cast<uint32_t>(parent.state.getSequence().size());
    const uint32_t seqLen = static_cast<uint32_t>(editSequence.size());
    assert(editOffset <= seqLen && "editOffset must lie inside transition sequence");
    CompositionState newState = states.afterEditTransition(
        parent.state, editSequence, goalPos, Mode::Normal);
    const int editIndex = editsAfter - 1;
    typename Trace::State childTrace = trace.onEditTransition(
        parent.trace, editIndex, base + editOffset, base + seqLen);
    enqueueState(std::move(newState), std::move(childTrace));
  };
  auto enqueueMotionTransition = [&](const Entry& parent,
                                     const Sequence& moveSequence,
                                     const CursorPos& goalPos) {
    CompositionState newState = states.afterNavResult(
        parent.state, moveSequence, goalPos);
    enqueueState(std::move(newState), trace.onMotionTransition(parent.trace));
  };

  // Slice a padded subset of `fromLines` covering the cursor and target lines,
  // and build the boundary the inner NavOptimizer will see. Used by both the
  // range-motion and point-motion exploration helpers below.
  auto sliceMotionSubset = [&](CursorPos pos, int targetBeginLine, int targetEndLine,
                               const Lines& fromLines) {
    struct Slice {
      Lines subset;
      int beginLine;
      int endLine;
      CursorPos localPos;
      NavBoundary subsetBoundary;
      BufferIndex bufferIndex;
      int bufferIndexLineOffset;
    };
    auto [beginLine, endLine] = fromLines.minmaxBoundWithPadding(
        min(pos.line, targetBeginLine), max(pos.line + 1, targetEndLine + 1),
        params.navPaddingAbove, params.navPaddingBelow);
    Lines subset = fromLines.getLineRange(beginLine, endLine);
    CursorPos localPos(pos.line - beginLine, pos.col, pos.targetCol);
    CursorPos subsetFirst(0, 0);
    CursorPos subsetEnd(static_cast<int>(subset.size()) - 1,
        subset.back().effectiveSize());
    NavBoundary subsetBoundary(subset, subsetFirst, subsetEnd,
        beginLine > 0 || boundary.hasLinesAbove(),
        endLine <= fromLines.lastLine() || boundary.hasLinesBelow());
    auto bufferIndexWindow = buildCompositionBufferIndexWindow(
        fromLines, beginLine, endLine);
    return Slice{
        std::move(subset),
        beginLine,
        endLine,
        localPos,
        std::move(subsetBoundary),
        std::move(bufferIndexWindow.index),
        bufferIndexWindow.lineOffset,
    };
  };

  // Enumerate motions from `pos` toward a CharInterval target.
  // `makeLocalInterval(subset, beginLine)` translates the caller's full-buffer
  // target intent into subset-local coords (returns nullopt to skip search).
  // - Multi-sink range targets (inter-edit motion, J-plan motion) use the
  //   default `keepMultiplePerLanding=false` so we get one cheapest path
  //   per landing.
  // - Single-cursor targets (post-final-edit nav) construct a 1-element
  //   interval and pass `keepMultiplePerLanding=true` so multiple distinct
  //   sequences to the single point are enumerated.
  auto exploreMotionsToInterval = [&](
      const Entry& parent, CursorPos pos,
      int targetBeginLine, int targetEndLine,
      const Lines& fromLines,
      int maxResults, bool keepMultiplePerLanding,
      auto&& makeLocalInterval) {
    auto slice = sliceMotionSubset(pos, targetBeginLine, targetEndLine, fromLines);
    CharInterval localInterval = makeLocalInterval(slice.subset, slice.beginLine);

    auto navParams = navParamsForCompositionMotion(params)
        .withMaxResults(maxResults)
        .withMaxResultsPerEndPos(keepMultiplePerLanding ? 2 : 1);
    auto navResult = navOptimizer.optimize(
        slice.subset, slice.localPos, localInterval,
        navParams, "", slice.subsetBoundary, navigationContext,
        slice.bufferIndex, slice.bufferIndexLineOffset);
    ctx.navNodesExplored += navResult.getStats().nodesExplored();

    for (const LandingResult& movResult : navResult.getResults()) {
      if (movResult.getSequence().empty()) continue;
      CursorPos goalPos = movResult.getGoalPos();
      goalPos.line += slice.beginLine;
      enqueueMotionTransition(parent, movResult.getSequence(), goalPos);
    }
  };

  debug("=== CompositionOptimizer A* search ===");

  while (!pq.empty() && ctx.totalPops < params.maxNodesPopped) {
    Entry current = pq.top();
    pq.pop();
    const CompositionState& s = current.state;
    ctx.totalPops++;
    CursorPos pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    if (editsCompleted == ctx.totalEdits() && pos == goalPos) {
      ctx.nodesProcessed++;
      double effort = s.getRunningEffort().getEffort(config);
      debug("GOAL #" + to_string(results.size()) + ":",
            "\"" + s.getSequence().str() + "\"", "effort:", effort);
      results.emplace_back(s.getSequence(), effort);
      // Single load-bearing `collecting` check left in optimizeImpl: NoTrace
      // emits empty span vectors that would otherwise pollute resultSpans.
      // All other tracer event sites go through unconditional method calls.
      if constexpr (Trace::collecting) {
        resultSpans.push_back(
            trace.onGoalReached(current.trace, ctx.totalEdits()));
      }
      if (results.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    }

    auto costIt = ctx.costMap.find(s.getKey());
    if (costIt != ctx.costMap.end() && costIt->second < s.getCost()) {
      ctx.statesSkipped++;
      continue;
    }

    ctx.nodesProcessed++;
    ctx.trackState(s);

    debug("pop:", "\"" + s.getSequence().str() + "\"",
          "pos:", pos, "edits:", editsCompleted,
          "cost:", s.getCost(), "effort:", s.getEffort());

    // ========== POST-FINAL-EDIT NAV PHASE ==========
    // All edits applied but cursor isn't at goalPos yet. Enumerate motions
    // from `pos` toward `goalPos` and enqueue, keeping editsCompleted fixed
    // at totalEdits(). No edit transitions are explored here.
    if (editsCompleted == ctx.totalEdits()) {
      debug("  post-edit nav from", pos, "to goalPos", goalPos);
      exploreMotionsToInterval(
          current, pos, goalPos.line, goalPos.line,
          ctx.getLinesAfter(editsCompleted),
          clamp(params.maxResults, 1, 10), /*keepMultiplePerLanding=*/true,
          [&](const Lines& subset, int beginLine) -> CharInterval {
            CursorPos localGoal(goalPos.line - beginLine, goalPos.col, goalPos.targetCol);
            return CharInterval(localGoal, localGoal);
          });
      continue;
    }

    // Get current buffer state
    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);
    const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

    // ========== PURE INSERTION HANDLING ==========
    // Pure insertions have no edit region to transition into. Strategies
    // come from CompositionStrategies::enumerateInsertions — same source
    // CompositionFrontier consumes — so the depth-1 frontier and the full
    // optimizer never drift.
    if (nextEdit.isPureInsertion()) {
      debug("  pure insertion at", nextEdit.beginPos,
            "text='" + VF::prettify(nextEdit.insertedText) + "'");

      // Dispatch one strategy: enqueue zero-prefix edit when cursor is already
      // in range, otherwise run NavOptimizer to find motion prefixes and
      // enqueue motion-prefixed edit transitions for each result.
      auto dispatchInsertion = [&](const CompositionStrategies::Insertion& s) {
        const bool inRange = (pos.line == s.targetLine &&
                              pos.col >= s.beginCol && pos.col < s.endCol);

        if (inRange) {
          // No motion prefix: editOffset = 0 — the whole transition sequence
          // is the planned edit.
          enqueueEditTransition(
              current, Sequence(s.insertCmd), s.goalPos, editsCompleted + 1);
          return;
        }

        auto search = buildCompositionRangeMotionSearch(
            currentLines, pos, s.targetLine, s.beginCol, s.endCol,
            boundary, params);
        if (!search) return;
        auto navResult = optimizeCompositionRangeMotion(
            navOptimizer, *search, navigationContext, params, 1);
        ctx.navNodesExplored += navResult.getStats().nodesExplored();

        for (const LandingResult& movResult : navResult.getResults()) {
          if (movResult.getSequence().empty()) continue;

          const CursorPos& localGoal = movResult.getGoalPos();
          assert(localGoal >= search->localRangeBegin && localGoal < search->localRangeEnd &&
                 "pure insertion motion goal must be subset-local and inside target range");
          // Intentionally do not remap localGoal to full-buffer coordinates here:
          // this branch immediately appends the insertion and transitions using
          // the strategy's post-insert position, so the intermediate motion
          // endpoint isn't consumed.
          //
          // Tracing: the prefix (motion) is Navigate; only the insertCmd suffix
          // is the planned edit. editOffset = motion length so the recorded edit
          // span covers only the insertion, leaving the motion as a nav row.
          Sequence fullSeq = movResult.getSequence();
          const uint32_t motionBytes =
              static_cast<uint32_t>(fullSeq.size());
          fullSeq.append(s.insertCmd);
          enqueueEditTransition(
              current, fullSeq, s.goalPos, editsCompleted + 1, motionBytes);
        }
      };

      CompositionStrategies::enumerateInsertions(nextEdit, currentLines, dispatchInsertion);
      continue;
    }

    // ========== EDIT vs MOVEMENT TRANSITIONS ==========
    const TransformResult& transformResult = ctx.edits[editsCompleted].transformResult;
    std::span<const Result> editAlternatives =
        transformResult.resultsAt(pos.line, pos.col);
    bool canUseTransformAlternatives = true;
    if (!editAlternatives.empty() && pos.targetCol != pos.col) {
      // Transform buckets are keyed by visible pos; linewise edits also depend
      // on curswant. Reset exactly when `0` is a no-visible-move command.
      canUseTransformAlternatives = false;
      if (pos.col == 0) {
        enqueueMotionTransition(current, Sequence("0"), CursorPos(pos.line, 0));
      }
    }

    if (canUseTransformAlternatives) {
      for (size_t resultIndex = 0; resultIndex < editAlternatives.size(); resultIndex++) {
        const Result& res = editAlternatives[resultIndex];
        CursorPos editGoalPos = transformResult.goalPosAt(
            pos.line, pos.col, resultIndex);
        if (transformResult.hasResultGoals()) {
          editGoalPos = clampGoalPosToLines(editGoalPos, ctx.getLinesAfter(editsCompleted + 1));
        }
        debug("  edit found at", pos, "seq:", "\"" + res.getSequence().str() + "\"",
              "cost:", res.getCost(), "goalPos:", editGoalPos);
        enqueueEditTransition(current, res.getSequence(),
                              editGoalPos, editsCompleted + 1);
      }
    }

    const BracketQuoteContext& bqContext = ctx.edits[editsCompleted].bracketQuoteContext;
    if (bqContext.line == pos.line) {
      CompositionStrategies::enumerateBracketQuotes(
          nextEdit, currentLines, bqContext,
          [&](const CompositionStrategies::BracketQuote& s) {
            if (s.line != pos.line || s.col != pos.col) return;
            debug("    text-object strategy at col", s.col, ":", s.body);
            enqueueEditTransition(
                current, Sequence(s.body), s.goalPos, editsCompleted + 1);
          });
    }

    // J plans always start with `J`, so activation is column-insensitive.
    const auto& joinPlan = ctx.edits[editsCompleted].joinPlan;
    if (joinPlan && pos.line == joinPlan->entryLine) {
      debug("  J plan at", pos, "seq:", "\"" + joinPlan->sequence.str() + "\"",
            "effort:", joinPlan->effort);
      enqueueEditTransition(
          current, joinPlan->sequence, joinPlan->goalPos, editsCompleted + 1);
    }

    if (editAlternatives.empty()) {
      if (joinPlan && pos.line != joinPlan->entryLine) {
        int entryLine = joinPlan->entryLine;
        exploreMotionsToInterval(
            current, pos, entryLine, entryLine, currentLines, /*maxResults=*/1,
            /*keepMultiplePerLanding=*/false,
            [&](const Lines& subset, int beginLine) -> CharInterval {
              int localLine = entryLine - beginLine;
              return CharInterval(CursorPos(localLine, 0),
                                  CursorPos(localLine, subset[localLine].lastCol()));
            });
      }

      vector<CursorPos> starts = transformResult.startPositions();
      std::sort(starts.begin(), starts.end(), [&](CursorPos a, CursorPos b) {
        return ctx.costToGoal(pos, a) < ctx.costToGoal(pos, b);
      });

      int searchedStarts = 0;
      for (CursorPos start : starts) {
        if (start == pos) continue;
        exploreMotionsToInterval(
            current, pos, start.line, start.line, currentLines,
            /*maxResults=*/1, /*keepMultiplePerLanding=*/false,
            [&](const Lines&, int beginLine) -> CharInterval {
              CursorPos localStart(start.line - beginLine, start.col);
              return CharInterval(localStart, localStart);
            });
        if (++searchedStarts >= 8) break;
      }
      if (searchedStarts > 0) {
        continue;
      }

      debug("  motion search from", pos, "to edit region [" +
            to_string(nextEdit.beginPos.line) + "," + to_string(nextEdit.beginPos.col)
            + ")-[" + to_string(nextEdit.endPos.line) + "," +
            to_string(nextEdit.endPos.col) + ")");

      // Motions to the next edit's diff range.
      exploreMotionsToInterval(
          current, pos, nextEdit.beginPos.line, nextEdit.editEndLine() - 1,
          currentLines,
          clamp(nextEdit.origCharCount(), 1, 10),
          /*keepMultiplePerLanding=*/false,
          [&](const Lines& subset, int beginLine) -> CharInterval {
            CursorPos localBegin(nextEdit.beginPos.line - beginLine, nextEdit.beginPos.col);
            CursorPos localEnd(nextEdit.endPos.line - beginLine, nextEdit.endPos.col);
            return CharInterval(CharRange(localBegin, localEnd), subset);
          });

    }
  }

  debug("=== CompositionOptimizer done ===");
  debug("results:", static_cast<int>(results.size()),
        "nodes:", ctx.nodesProcessed,
        "pops:", ctx.totalPops,
        "skipped:", ctx.statesSkipped,
        "queueRemaining:", static_cast<int>(pq.size()));

  const int pqRemaining = static_cast<int>(pq.size());
  const bool fullyExplored = pq.empty();
  // Stash the search-summary metrics on `ctx` via the wrapper that also
  // accepts pqRemaining/fullyExplored — we do this here, before pq goes out
  // of scope, then `buildCompositionResult` reads them back.
  ctx.lastPqRemaining = pqRemaining;
  ctx.lastFullyExplored = fullyExplored;
  return std::pair{std::move(results), std::move(resultSpans)};
}

} // anonymous namespace (templated optimizeImpl)

// Build a CompositionResult from the search outputs plus the precomputed
// per-edit data still owned by `ctx`. Consumes `ctx.edits` (moves diffStates
// and transformResults out) and `ctx`'s explored states; do not touch ctx
// after calling.
static CompositionResult buildCompositionResult(
    vector<Result> results,
    const Lines& initialLines, CursorPos goalPos,
    CompositionSearchContext& ctx) {
  vector<Lines> fenceposts;
  vector<DiffState> diffs;
  vector<TransformResult> transformResults;
  fenceposts.reserve(ctx.edits.size() + 1);
  diffs.reserve(ctx.edits.size());
  transformResults.reserve(ctx.edits.size());
  fenceposts.push_back(initialLines);
  for (auto& e : ctx.edits) {
    fenceposts.push_back(Myers::applyDiffState(e.diffState, fenceposts.back()));
    diffs.push_back(std::move(e.diffState));
    transformResults.push_back(std::move(e.transformResult));
  }

  int numResults = static_cast<int>(results.size());
  CompositionPlan plan{
      .finalGoalPos = goalPos,
      .diffs = std::move(diffs),
      .fenceposts = std::move(fenceposts),
  };
  return CompositionResult(std::move(results),
                           ctx.getStats(numResults,
                                        ctx.lastPqRemaining,
                                        ctx.lastFullyExplored),
                           std::move(plan),
                           ctx.takeExploredStates(),
                           std::move(transformResults));
}

// `goalPos` is a real terminal target: the search continues past the last edit
// with motion-only transitions until pos == goalPos. For pure-motion sessions
// (E == 0), the search starts directly in the post-final-edit nav phase.
CompositionResult CompositionOptimizer::optimize(
    const Lines& initialLines, const CursorPos initialPos, const Lines& goalLines,
    const CursorPos goalPos, CompositionOptimizerParams params,
    string_view userSequence, const NavBoundary& boundary,
    const NavContext& navigationContext) {
  CHECK(goalLines.contains(goalPos),
        "goalPos must be a valid normal-mode cursor position in goalLines");
  if(goalPos < initialPos) {
    debug("only support forward motion in CompositionOptimizer");
  }

  const bool timePhases = std::getenv("VIMFY_TIME_PHASES") != nullptr;
  auto setupT0 = std::chrono::steady_clock::now();
  CompositionSearchContext ctx(initialLines, initialPos, goalLines, goalPos,
                               userSequence, navigationContext, boundary,
                               params, config);
  if (timePhases) {
    std::cerr << "[phase] setup (ctx ctor) total: "
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - setupT0).count()
              << " ms, diffs=" << ctx.totalEdits() << "\n";
  }

  if (ctx.totalEdits() > 16) {
    debug("Cannot support more than 16 edits");
    return {};
  }

  NoTrace trace;
  auto loopT0 = std::chrono::steady_clock::now();
  auto out = optimizeImpl<NoTrace>(
      config, initialLines, initialPos, goalLines, goalPos,
      params, userSequence, boundary, navigationContext, ctx, trace);
  if (timePhases) {
    std::cerr << "[phase] A* loop total: "
              << std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - loopT0).count()
              << " ms\n";
  }
  if (out.first.empty()) {
    if (auto fallback = buildWholeBufferRewriteFallback(
            config, initialLines, goalLines, goalPos, navigationContext)) {
      out.first.push_back(std::move(*fallback));
    }
  }
  return buildCompositionResult(std::move(out.first), initialLines, goalPos, ctx);
}

CompositionTraceResult CompositionOptimizer::optimizeWithEditSpans(
    const Lines& initialLines, const CursorPos initialPos, const Lines& goalLines,
    const CursorPos goalPos, CompositionOptimizerParams params,
    string_view userSequence, const NavBoundary& boundary,
    const NavContext& navigationContext) {
  CHECK(goalLines.contains(goalPos),
        "goalPos must be a valid normal-mode cursor position in goalLines");
  if(goalPos < initialPos) {
    debug("only support forward motion in CompositionOptimizer");
  }

  CompositionSearchContext ctx(initialLines, initialPos, goalLines, goalPos,
                               userSequence, navigationContext, boundary,
                               params, config);

  if (ctx.totalEdits() > 16) {
    debug("Cannot support more than 16 edits");
    return {};
  }

  EditSpanTrace trace;
  auto out = optimizeImpl<EditSpanTrace>(
      config, initialLines, initialPos, goalLines, goalPos,
      params, userSequence, boundary, navigationContext, ctx, trace);
  if (out.first.empty()) {
    if (auto fallback = buildWholeBufferRewriteFallback(
            config, initialLines, goalLines, goalPos, navigationContext)) {
      out.first.push_back(std::move(*fallback));
      out.second.emplace_back(static_cast<size_t>(ctx.totalEdits()));
    }
  }
  CompositionTraceResult result;
  result.editSpansByResult = std::move(out.second);
  result.result = buildCompositionResult(
      std::move(out.first), initialLines, goalPos, ctx);
  assert(result.editSpansByResult.size() == result.result.getResults().size() &&
         "traced edit spans must align 1:1 with results");
  return result;
}

ostream& operator<<(ostream& os, const CompositionResult& cr) {
  os << cr.getStats() << " goalPos=" << cr.getGoalPos() << "\n";
  const auto& diffs = cr.getDiffs();
  const auto& results = cr.getResults();

  // Print diff legend: all diffs get sequential {n} labels.
  if (!diffs.empty()) {
    os << "Diffs:";
    for (size_t i = 0; i < diffs.size(); i++) {
      os << " {" << i << "}=" << diffs[i];
    }
    os << "\n";
  }

  for (size_t i = 0; i < results.size(); i++) {
    os << "  [" << i << "] "
       << VF::prettify(results[i].getSequence().view())
       << " " << results[i].getCost() << "\n";
  }

  return os;
}
