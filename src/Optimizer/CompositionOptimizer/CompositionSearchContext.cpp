#include "CompositionSearchContext.h"

#include "Keyboard/CharToKeys.h"
#include "Utils/Debug.h"

#include <algorithm>
#include <cassert>
#include <limits>

using namespace std;

CompositionSearchContext::CompositionSearchContext(
    const Lines& initialLines,
    const Position& startPos,
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
  assert(totalEdits <= MAX_EDITS && "Too many edits for bitmask representation");

  // Determine processing direction based on start position relative to edits
  if (!rawDiffs.empty()) {
    double distToFirst = costToGoal(startPos, rawDiffs.front().firstPos);
    double distToLast = costToGoal(startPos, rawDiffs.back().lastPos);
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

  // Build position -> edit index map
  int maxLineSize = 0;
  for (const auto& lines : linesAfterNEdits) {
    maxLineSize = max(maxLineSize, static_cast<int>(lines.size()));
  }
  int maxPosKey = maxLineSize * maxLineLength;
  posToEditIndex = buildPosToEditIndex(maxPosKey);
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
        col += diff.firstPos.col;
      }
      return Position(diff.newLineStart() + i, col);
    }
    remaining -= lineLen;
  }

  // Index was at end of last line
  int lastLine = diff.newLineStart() + static_cast<int>(inserted.size()) - 1;
  int lastCol = inserted.empty() ? diff.firstPos.col
                                 : static_cast<int>(inserted.back().size());
  if (!inserted.empty() && inserted.size() == 1) {
    lastCol += diff.firstPos.col;
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

    if (pos < nextEdit.firstPos) {
      // Undershooting: normal cost to reach the edit
      h += costToGoal(pos, nextEdit.firstPos);
    } else if (pos > nextEdit.lastPos) {
      // Overshooting: went past the edit, heavily penalized
      h += overshootPenalty * costToGoal(pos, nextEdit.lastPos);
    }
    // else: inside range, distance = 0
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

bool CompositionSearchContext::canStartEdit(
    const Position& pos, int editsCompleted) const {
  // Pure insertions are handled separately, not through edit transitions
  if (diffStates[editsCompleted].isPureInsertion()) {
    return false;
  }
  int posKey = this->posToKey(pos);
  if (posKey < 0 || posKey >= static_cast<int>(posToEditIndex.size())) {
    return false;
  }
  return (posToEditIndex[posKey] >> editsCompleted) & 1;
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

    if (!editResults[i].has_value()) {
      // Pure insertion: estimate cost as insert mode entry + text length + escape
      // i (1) + text + Esc (1)
      medianCost = 1.0 + static_cast<double>(diffStates[i].insertedText.size()) + 1.0;
    } else {
      const auto& editRes = *editResults[i];
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
    }
    suffixCosts[i] = suffixCosts[i + 1] + medianCost;
  }

  return suffixCosts;
}

vector<uint16_t> CompositionSearchContext::buildPosToEditIndex(
    int maxPosKey) const {
  vector<uint16_t> result(maxPosKey, 0);

  for (int editIdx = 0; editIdx < static_cast<int>(diffStates.size());
       editIdx++) {
    const DiffState& diff = diffStates[editIdx];
    uint16_t editMask = static_cast<uint16_t>(1 << editIdx);

    if (diff.isPureInsertion()) {
      // Pure insertion: mark only the insertion point
      int posKey = posToKey(diff.firstPos);
      if (posKey >= 0 && posKey < maxPosKey) {
        result[posKey] |= editMask;
      }
    } else {
      // Deletion or replacement: mark positions from firstPos to lastPos
      for (int line = diff.firstPos.line; line <= diff.lastPos.line; line++) {
        int startCol = (line == diff.firstPos.line) ? diff.firstPos.col : 0;
        int endCol =
            (line == diff.lastPos.line) ? diff.lastPos.col : maxLineLength - 1;

        for (int col = startCol; col <= endCol; col++) {
          int posKey = line * maxLineLength + col;
          if (posKey >= 0 && posKey < maxPosKey) {
            result[posKey] |= editMask;
          }
        }
      }
    }
  }

  return result;
}

vector<std::optional<EditResult>> CompositionSearchContext::calculateEditResults() {
  EditOptimizer editOptimizer(config);
  vector<std::optional<EditResult>> results;
  results.reserve(diffStates.size());

  for (const DiffState& diff : diffStates) {
    // Skip EditOptimizer for pure insertions - handled specially in CompositionOptimizer
    if (diff.isPureInsertion()) {
      results.push_back(std::nullopt);
      continue;
    }

    EditResult result = editOptimizer.optimizeEdit(
        diff.deletedLines(), diff.insertedLines(), diff.boundary);

    // Compute lineBaseIndex for O(1) buffer position to flat index lookup
    result.computeLineBaseIndex(diff.deletedLines(), diff.firstPos.line, diff.firstPos.col);

    // Compute cursor position after edit completes
    // After change + typed text + <Esc>, cursor is at last char of inserted text
    const Lines& inserted = diff.insertedLines();
    if (inserted.empty() || (inserted.size() == 1 && inserted[0].empty())) {
      // Pure deletion or empty insertion: cursor at start of edit region
      result.endPos = diff.firstPos;
    } else if (inserted.size() == 1) {
      // Single line: cursor at last char of inserted text
      result.endPos = Position(diff.firstPos.line,
                               diff.firstPos.col + static_cast<int>(inserted[0].size()) - 1);
    } else {
      // Multi-line: cursor at last char of last inserted line
      int lastLine = diff.firstPos.line + static_cast<int>(inserted.size()) - 1;
      int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
      result.endPos = Position(lastLine, lastCol);
    }

    // Check if replacement strategy is applicable and better
    if (diff.deletedText.size() == diff.insertedText.size() &&
        diff.deletedText.find('\n') == string::npos &&
        diff.insertedText.find('\n') == string::npos &&
        !diff.deletedText.empty() && diff.deletedText != diff.insertedText) {

      vector<Result> replResults;
      int lastReplacementPos = -1;
      tryReplacement(diff.deletedText, diff.insertedText, config,
                     lastReplacementPos, replResults);

      if (!replResults.empty() && replResults[0].isValid()) {
        Result replResult = replResults[0];

        if (!result.typeAllResults.empty() &&
            result.typeAllResults[0].isValid()) {
          // Calculate deletion + typing cost
          double typingCost = 0;
          for (char c : diff.insertedText) {
            auto it = CHAR_TO_KEYS.find(c);
            if (it != CHAR_TO_KEYS.end()) {
              typingCost += it->second.size();
            }
          }
          double deletionTotalCost = result.typeAllResults[0].keyCost + typingCost;

          if (replResult.keyCost < deletionTotalCost) {
            result.typeAllResults[0] = replResult;
            debug("Replacement is cheaper for diff: ", diff.deletedText,
                  " -> ", diff.insertedText, " (", replResult.keyCost, " vs ",
                  deletionTotalCost, ")");
          }
        }
      }
    }

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
