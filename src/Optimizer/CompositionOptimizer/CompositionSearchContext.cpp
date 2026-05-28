#include "CompositionSearchContext.h"

#include <algorithm>
#include <cassert>
#include <limits>

#include "PlannedEditArtifacts.h"
#include "TreeDiff.h"
#include "Utils/Debug.h"
#include "Utils/PrettyText.h"

using namespace std;

CompositionSearchContext::CompositionSearchContext(
    const Lines& initialLines,
    const CursorPos& initialPos,
    const Lines& goalLines,
    const CursorPos& goalPos,
    string_view userSequence,
    const NavContext& navContext,
    const NavBoundary& boundary,
    const CompositionOptimizerParams& params,
    const Config& config)
    : config(config),
      params(params),
      navContext(navContext),
      boundary(boundary),
      goalPos(goalPos),
      overshootPenalty(params.overshootPenalty),
      maxLineLength(1000),
      effortWeight(params.effortWeight),
      distanceWeight(params.distanceWeight),
      maxEffort(userSequence.empty()
                    ? std::numeric_limits<double>::max()
                    : getEffort(userSequence, config) * params.exploreFactor) {

  // Ensure proper hashing - line lengths must fit in maxLineLength
  for (const string& s : initialLines) {
    assert(s.size() < static_cast<size_t>(maxLineLength - 10));
  }
  for (const string& s : goalLines) {
    assert(s.size() < static_cast<size_t>(maxLineLength - 10));
  }

  // Generate planned edit regions between start and end buffers.
  vector<DiffState> rawDiffs;
  switch (params.diffAlgorithm) {
    case DiffAlgorithm::Myers:
      rawDiffs = Myers::calculate(
          Lines(initialLines.begin(), initialLines.end()),
          Lines(goalLines.begin(), goalLines.end()));
      break;
    case DiffAlgorithm::Tree:
      rawDiffs = TreeDiff::calculate(
          Lines(initialLines.begin(), initialLines.end()),
          Lines(goalLines.begin(), goalLines.end()),
          config,
          TreeDiff::CostOptions{
              .diffOpenPenalty = params.treeDiffOpenPenalty,
          });
      break;
    default:
      CHECK(false, "unknown composition diff algorithm");
  }
  debug("diff algorithm:", DiffAlgorithm::name(params.diffAlgorithm));

  // Preserve the historical Myers processing-order heuristic. The tree diff
  // planner is intentionally forward-only for its first draft.
  if (params.diffAlgorithm == DiffAlgorithm::Myers && !rawDiffs.empty()) {
    double distToFirst = costToGoal(initialPos, rawDiffs.front().beginPos);
    double distToLast = costToGoal(initialPos, rawDiffs.back().endPos);
    bool processForward = (distToFirst <= distToLast + 1.0);  // Slight bias toward forward

    if (!processForward) {
      std::reverse(rawDiffs.begin(), rawDiffs.end());
      debug("Processing edits in reverse order (backward)");
    }
  }

  // Build per-edit data from raw diffs
  edits.reserve(rawDiffs.size());
  for (auto& d : rawDiffs) {
    edits.push_back(PerEditData{std::move(d)});
  }

  debug("=== CompositionOptimizer setup ===");
  debug("totalEdits:", totalEdits(), " initialPos:", initialPos,
        " maxEffort:", maxEffort);
  for (int i = 0; i < totalEdits(); i++) {
    const DiffState& d = edits[i].diffState;
    const char* kind = d.isPureInsertion() ? "INS" :
                       d.isPureDeletion()  ? "DEL" : "REP";
    debug("  diff[" + to_string(i) + "]:", kind,
          "at [" + to_string(d.beginPos.line) + "," + to_string(d.beginPos.col) + ")-["
          + to_string(d.endPos.line) + "," + to_string(d.endPos.col) + ")",
          "del='" + VF::prettify(d.deletedText) + "'",
          "ins='" + VF::prettify(d.insertedText) + "'");
  }

  // Build intermediate buffer states
  linesAfterNEdits_ = calculateLinesAfterDiffs(initialLines);

  // Solve each edit region
  calculateTransformResults();

  debug("--- edit results ---");
  for (int i = 0; i < totalEdits(); i++) {
    const auto& er = edits[i].transformResult;
    int validCount = 0;
    double bestCost = numeric_limits<double>::max();
    for (const auto& bucket : er.getResults()) {
      if (!bucket.empty()) {
        validCount++;
        bestCost = min(bestCost, bucket[0].getCost());
      }
    }
    debug("  edit[" + to_string(i) + "]:",
          validCount, "results, best cost:",
          validCount > 0 ? bestCost : -1.0,
          "goalPos:", er.getGoalPos());
  }

  // Compute J (join lines) plans
  computeJoinPlans();

  debug("--- join plans ---");
  for (int i = 0; i < totalEdits(); i++) {
    if (edits[i].joinPlan) {
      debug("  joinPlan[" + to_string(i) + "]: seq='" + edits[i].joinPlan->sequence.str() + "'",
            "effort:", edits[i].joinPlan->effort, "entryLine:", edits[i].joinPlan->entryLine,
            "goalPos:", edits[i].joinPlan->goalPos);
    }
  }

  // Compute text object contexts for shortcuts
  computeTextObjectContexts();

  for (int i = 0; i < totalEdits(); i++) {
    if (edits[i].bracketQuoteContext.hasAnyValid()) {
      debug("  textObj[" + to_string(i) + "]: active on line",
            edits[i].bracketQuoteContext.line);
    }
  }

  // Compute suffix sums for heuristic
  suffixEditCosts_ = computeSuffixEditCosts();

  debug("--- suffix edit costs ---");
  for (int i = 0; i <= totalEdits(); i++) {
    debug("  suffixCost[" + to_string(i) + "]:", suffixEditCosts_[i]);
  }
  debug("=== setup complete ===");
}

CursorPos CompositionSearchContext::editIndexToBufferPos(
    int flatIndex, const DiffState& diff) const {
  Lines inserted = diff.insertedLines();

  int remaining = flatIndex;
  for (int i = 0; i < static_cast<int>(inserted.size()); i++) {
    int lineLen = static_cast<int>(inserted[i].size());
    if (remaining < lineLen) {
      int col = remaining;
      if (i == 0) {
        col += diff.beginPos.col;
      }
      return CursorPos(diff.newLineStart() + i, col);
    }
    remaining -= lineLen;
  }

  // Index was at end of last line
  int lastLine = diff.newLineStart() + static_cast<int>(inserted.size()) - 1;
  int lastCol = inserted.empty() ? diff.beginPos.col
                                 : static_cast<int>(inserted.back().size());
  if (!inserted.empty() && inserted.size() == 1) {
    lastCol += diff.beginPos.col;
  }
  return CursorPos(lastLine, lastCol);
}

double CompositionSearchContext::heuristic(
    const CompositionState& s, int editsCompleted) const {
  // h(n) = distance to next edit region (or goalPos, post-final-edit)
  //      + suffix sum of edit costs
  // O(1) lookup for remaining edit costs
  double h = suffixEditCosts_[editsCompleted];

  CursorPos pos = s.getPos();

  if (editsCompleted < totalEdits()) {
    // Add distance to next edit region with asymmetric penalty
    const DiffState& nextEdit = edits[editsCompleted].diffState;

    if (nextEdit.beginPos == nextEdit.endPos) {
      // Pure insertion: only beginPos is valid entry point
      if (pos != nextEdit.beginPos) {
        h += costToGoal(pos, nextEdit.beginPos);
      }
    } else {
      // Regular edit: half-open range [beginPos, endPos)
      if (pos < nextEdit.beginPos) {
        // Undershooting: normal cost to reach the edit
        h += costToGoal(pos, nextEdit.beginPos);
      } else if (pos >= nextEdit.endPos) {
        // Overshooting: endPos is one past valid, add 1 to distance for penalty
        h += overshootPenalty * (costToGoal(pos, nextEdit.endPos) + 1);
      }
      // else: inside range [beginPos, endPos), distance = 0
    }
  } else {
    // Post-final-edit nav segment: distance to the user's goalPos.
    if (pos != goalPos) h += costToGoal(pos, goalPos);
  }

  return effortWeight * s.getEffort() + distanceWeight * h;
}

CompositionSearchStats CompositionSearchContext::getStats(int resultsFound,
                                                          int pqRemaining,
                                                          bool fullyExplored) const {
  CompositionSearchStats stats;
  SearchStopReason stopReason = SearchStopReason::Unknown;

  if (resultsFound >= params.maxResults) {
    stopReason = SearchStopReason::MaxResultsFound;
  } else if (totalPops >= params.maxNodesPopped) {
    stopReason = SearchStopReason::MaxPopsReached;
  } else if (fullyExplored) {
    stopReason = SearchStopReason::FullyExplored;
  }

  stats.finalize(nodesProcessed,
                        totalPops,
                        resultsFound,
                        pqRemaining,
                        stopReason);
  stats.setDebugSummary(0, statesSkipped);
  stats.setNodeBreakdown(navNodesExplored, editNodesExplored);

  return stats;
}

vector<double> CompositionSearchContext::computeSuffixEditCosts() const {
  int n = totalEdits();
  vector<double> suffixCosts(n + 1, 0.0);

  for (int i = n - 1; i >= 0; i--) {
    double medianCost;

    // All edits now have TransformResult (including pure insertions)
    const auto& editRes = edits[i].transformResult;
    vector<double> costs;
    for (const auto& bucket : editRes.getResults()) {
      if (!bucket.empty()) {
        costs.push_back(bucket[0].getCost());
      }
    }

    // Include J plan effort if available
    if (edits[i].joinPlan) {
      costs.push_back(edits[i].joinPlan->effort);
    }

    if (costs.empty()) {
      medianCost = 100.0;  // Fallback for empty results
    } else {
      size_t mid = costs.size() / 2;
      nth_element(costs.begin(), costs.begin() + mid, costs.end());
      medianCost = costs[mid];
    }

    suffixCosts[i] = suffixCosts[i + 1] + medianCost;
  }

  return suffixCosts;
}

void CompositionSearchContext::calculateTransformResults() {
  for (size_t i = 0; i < edits.size(); i++) {
    const DiffState& diff = edits[i].diffState;
    int nodesExplored = 0;
    edits[i].transformResult = computeTransformResultForDiff(diff, params, config, &nodesExplored);
    editNodesExplored += nodesExplored;
  }
}

vector<Lines> CompositionSearchContext::calculateLinesAfterDiffs(
    const Lines& initialLines) {
  vector<Lines> result(totalEdits() + 1);
  result[0] = initialLines;

  OriginalDiffMapper mapper;

  for (int i = 0; i < totalEdits(); i++) {
    DiffState originalDiff = edits[i].diffState;
    edits[i].diffState = mapper.mapDiffToCurrent(
        originalDiff, initialLines, result[i]);

    result[i + 1] = Myers::applyDiffState(edits[i].diffState, result[i]);
    mapper.recordApplied(originalDiff, initialLines);
  }

  return result;
}

void CompositionSearchContext::computeTextObjectContexts() {
  for (int i = 0; i < totalEdits(); i++) {
    edits[i].bracketQuoteContext = computeTextObjectContextForDiff(
        edits[i].diffState, linesAfterNEdits_[i]);
  }
}

// =============================================================================
// J (Join Lines) Plan Computation
// =============================================================================

void CompositionSearchContext::computeJoinPlans() {
  for (int i = 0; i < totalEdits(); i++) {
    int nodesExplored = 0;
    edits[i].joinPlan = computeJoinPlanForDiff(
        edits[i].diffState, linesAfterNEdits_[i], params, config, &nodesExplored);
    editNodesExplored += nodesExplored;
  }
}
