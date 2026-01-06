#include "CompositionOptimizer.h"

#include "DiffState.h"
#include "EditOptimizer.h"
#include "MovementOptimizer.h"

#include "State/CompositionState.h"
#include "State/MotionState.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"
#include "Utils/Debug.h"

#include <cassert>
#include <functional>
#include <algorithm>

using namespace std;

vector<Result> CompositionOptimizer::optimize(
  const vector<string>& startLines,
  const Position startPos,
  const vector<string>& endLines,
  const Position endPos,
  const string& userSequence,
  const NavContext& navigationContext,
  const ImpliedExclusions& impliedExclusions,
  const MotionToKeys& rawMotionToKeys
) {
  // Ensures proper hashing later, and 10 is buffer in case we insert more text, then delete
  for(const string& s : startLines) { assert(s.size() < MAX_LINE_LENGTH-10); }
  for(const string& s : endLines) { assert(s.size() < MAX_LINE_LENGTH-10); }

  MotionToKeys motionToKeys = rawMotionToKeys;
  if(impliedExclusions.exclude_G) {
    motionToKeys.erase("G");
  }
  if(impliedExclusions.exclude_gg) {
    motionToKeys.erase("gg");
  }

  // Get minimal diff between start and end buffers
  vector<DiffState> rawDiffs = Myers::calculate(
      Lines(startLines.begin(), startLines.end()),
      Lines(endLines.begin(), endLines.end()));

  // If no edits needed, return empty (nothing to optimize)
  if (rawDiffs.empty()) {
    return {};
  }

  // Determine processing direction based on start position relative to edits.
  // Forward = process edits left->right (top->bottom)
  // Backward = process edits right->left (bottom->top)
  // If backward, we reverse the edit order so all subsequent logic is uniform.
  double distToFirst = costToGoal(startPos, rawDiffs.front().posBegin);
  double distToLast = costToGoal(startPos, rawDiffs.back().posEnd);
  bool forward = (distToFirst <= distToLast + FORWARD_BIAS);

  if (!forward) {
    std::reverse(rawDiffs.begin(), rawDiffs.end());
    debug("Processing edits in reverse order (backward)");
  }

  // Adjust indices for sequential application, so edit 2's indices are in buffer after edit 1 is applied
  vector<DiffState> diffStates = Myers::adjustForSequential(rawDiffs);

  int totalEdits = static_cast<int>(diffStates.size());

  // Build intermediate buffer states. [0] = no changes (same as startLines), [d] = all changes (same as endLines)
  vector<Lines> linesAfterNEdits = calculateLinesAfterDiffs(
      Lines(startLines.begin(), startLines.end()), diffStates, totalEdits);

  vector<EditResult> editResults = calculateEditResults(diffStates);

  // Compute suffix sums of min edit costs for O(1) heuristic lookup
  vector<double> suffixEditCosts = computeSuffixEditCosts(editResults);

  // Position -> editIndex map
  int maxLineSize = 0;
  for (const auto& lines : linesAfterNEdits) {
    maxLineSize = max(maxLineSize, static_cast<int>(lines.size()));
  }
  int maxPosKey = maxLineSize * MAX_LINE_LENGTH;
  vector<vector<int>> posToEditIndex = buildPosToEditIndex(diffStates, maxPosKey);

  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);

  vector<Result> res;
  unordered_map<CompositionStateKey, double, CompositionStateKeyHash> costMap;

  priority_queue<CompositionState, vector<CompositionState>, greater<CompositionState>> pq;

  auto exploreNewState = [this, &pq, &costMap, &userEffort, totalEdits](CompositionState&& newState) {
    if(newState.getEffort() > userEffort * EXPLORE_FACTOR) {
      return;
    }
    double newCost = newState.getCost();
    const CompositionStateKey newKey = newState.getKey();
    auto it = costMap.find(newKey);
    if(it == costMap.end()) {
      // Don't cache goal states (we want multiple results)
      if(newState.getEditsCompleted() != totalEdits) {
        costMap.emplace(newKey, newCost);
      }
      pq.push(std::move(newState));
    }
    else if (newCost <= it->second) {
      it->second = newCost;
      pq.push(std::move(newState));
    }
  };

  // Initialize starting state
  CompositionState startingState(startPos, Mode::Normal, 0);
  startingState.updateCost(heuristic(startingState, 0, suffixEditCosts, diffStates));
  pq.push(startingState);
  costMap[startingState.getKey()] = startingState.getCost();

  // Main search logic
  while(!pq.empty()) {
    CompositionState s = pq.top();
    pq.pop();
    Position pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();
    Mode mode = s.getMode();

    if(++totalExplored > MAX_SEARCH_DEPTH) {
      debug("maximum total explored count reached");
      break;
    }

    CompositionStateKey stateKey = s.getKey();
    bool isGoal = (editsCompleted == totalEdits);

    if(isGoal) {
      res.emplace_back(s.getMotionSequence(), s.getRunningEffort().getEffort(config));
      if(res.size() >= MAX_RESULT_COUNT) {
        debug("maximum result count reached");
        break;
      }
      continue;
    } else if(costMap.count(stateKey) && costMap[stateKey] < s.getCost()) {
      // Discard this since there's a better state
      continue;
    }

    // Get current buffer state
    const Lines& currentLines = linesAfterNEdits[editsCompleted];
    int numLines = static_cast<int>(currentLines.size());

    // ========== EDIT TRANSITIONS ==========
    // Check if we can perform the next edit from current position
    if (mode == Mode::Normal) {
      int posKey = posToKey(pos);
      if (posKey >= 0 && posKey < static_cast<int>(posToEditIndex.size())) {
        const vector<int>& validEdits = posToEditIndex[posKey];
        // Check if next edit (editsCompleted) is in the valid list
        if (find(validEdits.begin(), validEdits.end(), editsCompleted) != validEdits.end()) {
          const DiffState& diff = diffStates[editsCompleted];
          const EditResult& editResult = editResults[editsCompleted];

          // Convert buffer position to edit region index
          int i = bufferPosToEditIndex(pos, diff);
          if (i >= 0 && i < editResult.n) {
            // Try all possible ending positions
            for (int j = 0; j < editResult.m; j++) {
              const Result& editRes = editResult.adj[i][j];
              if (editRes.isValid()) {
                CompositionState newState = s;

                // Convert end index back to buffer position
                // NOTE: After this edit, we're in linesAfterNEdits[editsCompleted + 1]
                Position newPos = editIndexToBufferPos(j, diff);

                newState.applyEditTransition(editRes.sequence, newPos, config);
                newState.updateCost(heuristic(newState, editsCompleted + 1, suffixEditCosts, diffStates));
                exploreNewState(std::move(newState));
              }
            }
          }
        }
      }
    }

    // ========== MOVEMENT TRANSITIONS ==========
    // Use MovementOptimizer to find optimal paths to next edit region
    if (editsCompleted < totalEdits) {
      const DiffState& nextEdit = diffStates[editsCompleted];

      // Copy NavContext for motion application
      NavContext navContext = navigationContext;

      // Compute exclusions for this sub-search:
      // - Inherit parent exclusions
      // - Additionally exclude gg if target range doesn't include line 0
      // - Additionally exclude G if target range doesn't include last line
      int lastLine = numLines - 1;
      ImpliedExclusions subExclusions(
        impliedExclusions.exclude_G || nextEdit.posEnd.line < lastLine,
        impliedExclusions.exclude_gg || nextEdit.posBegin.line > 0
      );

      // Use MovementOptimizer to find optimal paths to any position in the edit region
      // Pass only Position and RunningEffort - sub-search computes its own effort/cost fresh
      // RangeResult.keyCost returns delta effort for this movement
      MovementOptimizer movementOptimizer(config);
      vector<RangeResult> movementResults = movementOptimizer.optimizeToRange(
        currentLines,
        pos,
        s.getRunningEffort(),
        nextEdit.posBegin,
        nextEdit.posEnd,
        "", // No user sequence reference for sub-optimization
        navContext,
        SearchParams(clamp(nextEdit.origCharCount(), 1, 10)),  // Max results per movement search
        false, // allowMultiplePerPosition: only need 1 best path per position
        subExclusions,
        motionToKeys
      );

      // Create new CompositionStates from movement results
      for (const RangeResult& movResult : movementResults) {
        if (!movResult.isValid()) continue;

        CompositionState newState = s;
        newState.applyMovementResult(movResult.sequence, movResult.endPos, config);
        newState.updateCost(heuristic(newState, editsCompleted, suffixEditCosts, diffStates));
        exploreNewState(std::move(newState));
      }
    }
  }

  return res;
}

vector<double> CompositionOptimizer::computeSuffixEditCosts(const vector<EditResult>& editResults) const {
  int n = static_cast<int>(editResults.size());
  vector<double> suffixCosts(n + 1, 0.0);

  // Compute median cost for each edit, then build suffix sums.
  // Using median is good for not being biased with large outliers.
  // How much cheaper the best edit costs are from this median is a good measure of desired exploredness
  for (int i = n - 1; i >= 0; i--) {
    const auto& editRes = editResults[i];
    vector<double> costs;
    for (int j = 0; j < editRes.n; j++) {
      for (int k = 0; k < editRes.m; k++) {
        if (editRes.adj[j][k].isValid()) {
          costs.push_back(editRes.adj[j][k].keyCost);
        }
      }
    }

    double medianCost;
    if (costs.empty()) {
      medianCost = 100.0;
    } else {
      size_t mid = costs.size() / 2;
      nth_element(costs.begin(), costs.begin() + mid, costs.end());
      medianCost = costs[mid];
    }
    suffixCosts[i] = suffixCosts[i + 1] + medianCost;
  }

  return suffixCosts;
}

double CompositionOptimizer::costToGoal(const Position& curr, const Position& goal) const {
  return abs(goal.line - curr.line) + abs(goal.col - curr.col);
}

double CompositionOptimizer::heuristic(const CompositionState& s, int editsCompleted,
                                        const vector<double>& suffixEditCosts,
                                        const vector<DiffState>& diffStates) const {
  // h(n) = distance to next edit region + suffix sum of edit costs
  // Overshooting (going past the next edit) is penalized more heavily than undershooting.
  // Note: If we're processing edits in reverse order, diffStates was already reversed,
  // so "overshooting" still means pos > nextEdit.posEnd in the processing direction.
  int totalEdits = static_cast<int>(diffStates.size());

  // O(1) lookup for remaining edit costs
  double h = suffixEditCosts[editsCompleted];

  // Add distance to next edit region with asymmetric penalty
  if (editsCompleted < totalEdits) {
    const DiffState& nextEdit = diffStates[editsCompleted];
    Position pos = s.getPos();
    if (pos < nextEdit.posBegin) {
      // Undershooting: normal cost to reach the edit
      h += costToGoal(pos, nextEdit.posBegin);
    } else if (pos > nextEdit.posEnd) {
      // Overshooting: went past the edit, heavily penalized
      // Must backtrack, which is inefficient
      h += OVERSHOOT_PENALTY * costToGoal(pos, nextEdit.posEnd);
    }
    // else: inside range, distance = 0
  }

  return COST_WEIGHT * s.getEffort() + h;
}

int CompositionOptimizer::bufferPosToEditIndex(const Position& bufferPos, const DiffState& diff) const {
  // Convert buffer position to flat index within deletedLines
  int editLine = bufferPos.line - diff.origLineStart();

  if (editLine < 0 || editLine >= static_cast<int>(diff.deletedLines.size())) {
    return -1;
  }

  // Compute flat index: sum of previous line lengths + column
  int flatIndex = 0;
  for (int i = 0; i < editLine; i++) {
    flatIndex += static_cast<int>(diff.deletedLines[i].size());
  }
  flatIndex += bufferPos.col;

  return flatIndex;
}

Position CompositionOptimizer::editIndexToBufferPos(int flatIndex, const DiffState& diff) const {
  // Convert flat index within insertedLines to buffer position
  // The new buffer has insertedLines at diff.newLineStart()

  int remaining = flatIndex;
  for (int i = 0; i < static_cast<int>(diff.insertedLines.size()); i++) {
    int lineLen = static_cast<int>(diff.insertedLines[i].size());
    if (remaining < lineLen) {
      // Found the line
      return Position(diff.newLineStart() + i, remaining);
    }
    remaining -= lineLen;
  }

  // If we get here, index was at end of last line
  int lastLine = diff.newLineStart() + static_cast<int>(diff.insertedLines.size()) - 1;
  int lastCol = diff.insertedLines.empty() ? 0 : static_cast<int>(diff.insertedLines.back().size());
  return Position(lastLine, lastCol);
}

vector<EditResult> CompositionOptimizer::calculateEditResults(const vector<DiffState>& diffStates) {
  EditOptimizer editOptimizer(config);
  vector<EditResult> results;
  results.reserve(diffStates.size());

  for (const DiffState& diff : diffStates) {
    EditResult result = editOptimizer.optimizeEdit(
        diff.deletedLines,
        diff.insertedLines,
        diff.boundary
    );
    results.push_back(std::move(result));
  }

  return results;
}

vector<Lines> CompositionOptimizer::calculateLinesAfterDiffs(const Lines& startLines, const vector<DiffState>& diffStates, int totalEdits) {
  assert(totalEdits == static_cast<int>(diffStates.size()));
  vector<Lines> res(totalEdits + 1);
  res[0] = startLines;
  for (int i = 1; i <= totalEdits; i++) {
    res[i] = Myers::applyDiffState(diffStates[i - 1], res[i - 1]);
  }
  return res;
}

vector<vector<int>> CompositionOptimizer::buildPosToEditIndex(
    const vector<DiffState>& diffStates,
    int maxPosKey
) {
  vector<vector<int>> posToEditIndex(maxPosKey);

  for (int editIdx = 0; editIdx < static_cast<int>(diffStates.size()); editIdx++) {
    const DiffState& diff = diffStates[editIdx];

    // Mark all positions on lines within the edit region
    // (line-based for compatibility with EditOptimizer's full-line indexing)
    for (int line = diff.origLineStart(); line < diff.origLineStart() + diff.origLineCount(); line++) {
      // Mark all columns on this line
      for (int col = 0; col < MAX_LINE_LENGTH; col++) {
        int posKey = line * MAX_LINE_LENGTH + col;
        if (posKey < maxPosKey) {
          posToEditIndex[posKey].push_back(editIdx);
        }
      }
    }

    // For pure insertions, mark the insertion point
    if (diff.origLineCount() == 0) {
      int posKey = diff.origLineStart() * MAX_LINE_LENGTH + 0;
      if (posKey < maxPosKey) {
        posToEditIndex[posKey].push_back(editIdx);
      }
    }
  }

  return posToEditIndex;
}
