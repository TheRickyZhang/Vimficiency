#include "CompositionOptimizer.h"

#include "CompositionSearchContext.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "State/CompositionState.h"
#include "Utils/Debug.h"

#include <cassert>

using namespace std;

vector<Result> CompositionOptimizer::optimize(
    const Lines& initialLines, const Position startPos, const Lines& goalLines,
    const Position endPos, const string& userSequence,
    const NavContext& navigationContext, const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys, CompositionOptimizerParams params) {

  // Create search context - handles all pre-computation
  CompositionSearchContext ctx(initialLines, startPos, goalLines, userSequence,
                               navigationContext, boundary, rawMotionToKeys,
                               params, config, overshootPenalty, forwardBias,
                               maxLineLength);

  if (ctx.totalEdits == 0) {
    return {};
  }
  if (ctx.totalEdits > 16) {
    debug("Cannot support more than 16 edits");
    return {};
  }

  vector<Result> results;

  // Initialize starting state with heuristic cost
  CompositionState startingState(startPos, Mode::Normal, 0);
  startingState.setCost(ctx.heuristic(startingState, 0));
  ctx.pq.push(startingState);
  ctx.costMap[startingState.getKey()] = startingState.getCost();

  while (ctx.shouldContinue()) {
    CompositionState s = ctx.popNext();
    Position pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();

    ctx.markProcessed();

    if (ctx.isGoal(s)) {
      results.emplace_back(s.getMotionSequence(),
                           s.getRunningEffort().getEffort(config));
      if (results.size() >= static_cast<size_t>(params.maxResults)) {
        debug("maximum result count reached");
        break;
      }
      continue;
    }

    if (ctx.isStale(s)) {
      ctx.statesSkipped++;
      continue;
    }

    // Get current buffer state
    const Lines& currentLines = ctx.getLinesAfter(editsCompleted);

    // TODO: Check for bracket/quote motions first

    // ========== EDIT TRANSITIONS ==========
    // Check if we can perform the next edit from current position
    if (ctx.canStartEdit(pos, editsCompleted)) {
      const EditResult& editResult = ctx.getEditResult(editsCompleted);

      // Convert buffer position to edit region index (O(1) lookup)
      int flatIdx = editResult.flatIndexAt(pos);
      assert(0 <= flatIdx &&
             flatIdx < static_cast<int>(editResult.typeAllResults.size()));
      const Result& editRes = editResult.typeAllResults[flatIdx];
      if (editRes.isValid()) {
        // Create new state with edit transition applied
        // Cursor ends at last char of inserted text (precomputed in editResult.endPos)
        CompositionState newState = s.afterEditTransition(
            editRes.sequences, editResult.endPos, Mode::Normal, config);
        newState.setCost(ctx.heuristic(newState, editsCompleted + 1));
        ctx.exploreNewState(std::move(newState));
      }
    }
    // ========== MOVEMENT TRANSITIONS ==========
    // Use MotionOptimizer to find optimal paths to next edit region
    else {
      const DiffState& nextEdit = ctx.getDiffState(editsCompleted);

      // Compute sub-boundary for motion search
      MotionBoundary subBoundary(currentLines, nextEdit.firstPos,
                                 nextEdit.lastPos, boundary);

      // Use MotionOptimizer to find paths to edit region
      MotionOptimizer movementOptimizer(config);
      vector<RangeResult> movementResults = movementOptimizer.optimizeToRange(
          currentLines, pos, s.getRunningEffort(), nextEdit.firstPos,
          nextEdit.lastPos,
          "", // No user sequence reference for sub-optimization
          navigationContext, subBoundary, ctx.motionToKeys,
          MotionOptimizerRangeParams{}.withMaxResults(
              clamp(nextEdit.origCharCount(), 1, 10))).results;

      // Create new states from movement results
      for (const RangeResult& movResult : movementResults) {
        if (!movResult.isValid())
          continue;

        CompositionState newState = s.afterMotionResult(
            movResult.sequences, movResult.endPos, config);
        newState.setCost(ctx.heuristic(newState, editsCompleted));
        ctx.exploreNewState(std::move(newState));
      }
    }
  }

  return results;
}
