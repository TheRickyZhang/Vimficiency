#include "CompositionSearchContext.h"

#include "Keyboard/MotionToKeys.h"
#include "State/RunningEffort.h"
#include "Utils/Debug.h"

#include <algorithm>
#include <cassert>
#include <limits>

using namespace std;

CompositionSearchContext::CompositionSearchContext(
    const Lines& initialLines,
    const Position& initialPos,
    const Lines& goalLines,
    const string& userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    const CompositionOptimizerParams& params,
    const Config& config,
    double overshootPenalty,
    double forwardBias,
    int maxLineLength)
    : config(config),
      params(params),
      navContext(navContext),
      boundary(boundary),
      motionToKeys(rawMotionToKeys),
      overshootPenalty(overshootPenalty),
      forwardBias(forwardBias),
      maxLineLength(maxLineLength),
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

  // Filter motion keys based on boundary
  if (boundary.hasLinesBelow()) {
    motionToKeys.erase("G");
  }
  if (boundary.hasLinesAbove()) {
    motionToKeys.erase("gg");
  }

  // Get minimal diff between start and end buffers
  vector<DiffState> rawDiffs = Myers::calculate(
      Lines(initialLines.begin(), initialLines.end()),
      Lines(goalLines.begin(), goalLines.end()));

  totalEdits = static_cast<int>(rawDiffs.size());

  // Determine processing direction based on start position relative to edits
  if (!rawDiffs.empty()) {
    double distToFirst = costToGoal(initialPos, rawDiffs.front().beginPos);
    double distToLast = costToGoal(initialPos, rawDiffs.back().endPos);
    forward = (distToFirst <= distToLast + forwardBias);

    if (!forward) {
      reverse(rawDiffs.begin(), rawDiffs.end());
      debug("Processing edits in reverse order (backward)");
    }

    // Adjust indices for sequential application
    diffStates = Myers::adjustForSequential(rawDiffs);
  }

  // Build intermediate buffer states
  linesAfterNEdits = calculateLinesAfterDiffs(initialLines);

  // Solve each edit region
  editResults = calculateEditResults();

  // Compute suffix sums for heuristic
  suffixEditCosts = computeSuffixEditCosts();
}

Position CompositionSearchContext::editIndexToBufferPos(
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
      return Position(diff.newLineStart() + i, col);
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
  return Position(lastLine, lastCol);
}

double CompositionSearchContext::heuristic(
    const CompositionState& s, int editsCompleted) const {
  // h(n) = distance to next edit region + suffix sum of edit costs
  // O(1) lookup for remaining edit costs
  double h = suffixEditCosts[editsCompleted];

  // Add distance to next edit region with asymmetric penalty
  if (editsCompleted < totalEdits) {
    const DiffState& nextEdit = diffStates[editsCompleted];
    Position pos = s.getPos();

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
  }

  return effortWeight * s.getEffort() + distanceWeight * h;
}

void CompositionSearchContext::exploreNewState(CompositionState&& newState) {
  if (newState.getEffort() > maxEffort) {
    return;
  }

  double newCost = newState.getCost();
  const CompositionStateKey newKey = newState.getKey();
  auto it = costMap.find(newKey);

  if (it == costMap.end()) {
    // Don't cache goal states (we want multiple results)
    if (newState.getEditsCompleted() != totalEdits) {
      costMap.emplace(newKey, newCost);
    }
    pq.push(std::move(newState));
  } else if (newCost <= it->second) {
    it->second = newCost;
    pq.push(std::move(newState));
  }
}

SearchStats CompositionSearchContext::getStats(int resultsFound) const {
  SearchStats stats;
  stats.nodesExplored = nodesProcessed;
  stats.resultsFound = resultsFound;
  stats.queueSizeAtStop = static_cast<int>(pq.size());
  stats.statesSkipped = statesSkipped;

  if (resultsFound >= params.maxResults) {
    stats.stopReason = SearchStopReason::MaxResultsFound;
  } else if (nodesProcessed >= params.maxNodesExplored) {
    stats.stopReason = SearchStopReason::MaxNodesReached;
  } else if (pq.empty()) {
    stats.stopReason = SearchStopReason::FullyExplored;
  }

  return stats;
}

vector<double> CompositionSearchContext::computeSuffixEditCosts() const {
  int n = static_cast<int>(editResults.size());
  vector<double> suffixCosts(n + 1, 0.0);

  for (int i = n - 1; i >= 0; i--) {
    double medianCost;

    // All edits now have EditResult (including pure insertions)
    const auto& editRes = editResults[i];
    vector<double> costs;
    for (const Result& r : editRes.typeAllResults) {
      if (r.isValid()) {
        costs.push_back(r.keyCost);
      }
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

// Helper: compute cursor position after insertion
// Cursor ends on last char of inserted text, or stays at insertPos if empty
static Position computeInsertEndPos(Position insertPos, const string& insertedText) {
  if (insertedText.empty()) {
    return insertPos;
  }
  Lines inserted = Lines::unflatten(insertedText);
  if (inserted.size() == 1) {
    // Single line: cursor at last char
    int endCol = insertPos.col + static_cast<int>(inserted[0].size()) - 1;
    return Position(insertPos.line, max(0, endCol));
  } else {
    // Multi-line: cursor at last char of last line
    int lastLine = insertPos.line + static_cast<int>(inserted.size()) - 1;
    int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
    return Position(lastLine, lastCol);
  }
}

vector<EditResult> CompositionSearchContext::calculateEditResults() {
  EditOptimizer editOptimizer(config);
  vector<EditResult> results;
  results.reserve(diffStates.size());

  for (const DiffState& diff : diffStates) {
    // Handle pure insertions: create single-entry EditResult with precomputed "i + text + <Esc>"
    if (diff.isPureInsertion()) {
      EditResult result(1);  // Single entry for position 0 (the insertion point)

      // Compute goalPos (same logic as regular edits)
      result.goalPos = computeInsertEndPos(diff.beginPos, diff.insertedText);

      // Build insert sequence: i + text + <Esc>
      string seq = "i" + diff.insertedText + "<Esc>";
      PhysicalKeys keys = globalTokenizer().tokenize(seq);
      double effort = RunningEffort().append(keys, config);
      result.typeAllResults[0] = Result(Sequence(seq), effort);

      // lineBaseIndex for single-point insertion
      result.firstLine = diff.beginPos.line;
      result.firstCol = diff.beginPos.col;
      result.lineBaseIndex = {-diff.beginPos.col};  // flatIndexAt(beginPos) == 0

      results.push_back(std::move(result));
      continue;
    }

    EditResult result = editOptimizer.optimizeEdit(
        diff.deletedLines(), diff.insertedLines(), diff.boundary);

    // Compute lineBaseIndex for O(1) buffer position to flat index lookup
    result.computeLineBaseIndex(diff.deletedLines(), diff.beginPos.line, diff.beginPos.col);

    // Compute cursor position after edit completes
    // After change + typed text + <Esc>, cursor is at last char of inserted text
    const Lines& inserted = diff.insertedLines();
    if (inserted.empty() || (inserted.size() == 1 && inserted[0].empty())) {
      // Pure deletion or empty insertion: cursor at start of edit region
      result.goalPos = diff.beginPos;
    } else if (inserted.size() == 1) {
      // Single line: cursor at last char of inserted text
      result.goalPos = Position(diff.beginPos.line,
                               diff.beginPos.col + static_cast<int>(inserted[0].size()) - 1);
    } else {
      // Multi-line: cursor at last char of last inserted line
      int lastLine = diff.beginPos.line + static_cast<int>(inserted.size()) - 1;
      int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
      result.goalPos = Position(lastLine, lastCol);
    }

    // Note: Replacement vs delete+type comparison is now handled inside EditOptimizer::optimizeEdit
    // The best result for position 0 is already stored in typeAllResults[0]

    results.push_back(std::move(result));
  }

  return results;
}

vector<Lines> CompositionSearchContext::calculateLinesAfterDiffs(
    const Lines& initialLines) const {
  assert(totalEdits == static_cast<int>(diffStates.size()));
  vector<Lines> result(totalEdits + 1);
  result[0] = initialLines;

  for (int i = 1; i <= totalEdits; i++) {
    result[i] = Myers::applyDiffState(diffStates[i - 1], result[i - 1]);
  }

  return result;
}
