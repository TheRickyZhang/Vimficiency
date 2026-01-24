#include "CompositionOptimizer.h"

#include "DiffState.h"
#include "EditOptimizer.h"
#include "MotionOptimizer.h"

#include "State/CompositionState.h"
#include "Keyboard/CharToKeys.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Lines.h"
#include "Utils/Debug.h"

#include <cassert>
#include <functional>
#include <algorithm>

using namespace std;

vector<Result> CompositionOptimizer::optimize(
  const Lines& initialLines,
  const Position startPos,
  const Lines& goalLines,
  const Position endPos,
  const string& userSequence,
  const NavContext& navigationContext,
  const MotionBoundary& boundary,
  const MotionToKeys& rawMotionToKeys,
  const optional<OptimizerParams>& paramsOverride
) {
  // Merge defaults with overrides
  const OptimizerParams params = OptimizerParams::merge(defaultParams, paramsOverride);

  // Ensures proper hashing later, and 10 is buffer in case we insert more text, then delete
  for(const string& s : initialLines) { assert(s.size() < static_cast<size_t>(maxLineLength-10)); }
  for(const string& s : goalLines) { assert(s.size() < static_cast<size_t>(maxLineLength-10)); }

  MotionToKeys motionToKeys = rawMotionToKeys;
  if(boundary.hasLinesBelow()) {
    motionToKeys.erase("G");
  }
  if(boundary.hasLinesAbove()) {
    motionToKeys.erase("gg");
  }

  // Get minimal diff between start and end buffers
  vector<DiffState> rawDiffs = Myers::calculate(
      Lines(initialLines.begin(), initialLines.end()),
      Lines(goalLines.begin(), goalLines.end()));

  // If no edits needed, return empty (nothing to optimize)
  if (rawDiffs.empty()) {
    return {};
  }

  // Determine processing direction based on start position relative to edits.
  // Forward = process edits left->right (top->bottom)
  // Backward = process edits right->left (bottom->top)
  // If backward, we reverse the edit order so all subsequent logic is uniform.
  double distToFirst = costToGoal(startPos, rawDiffs.front().firstPos);
  double distToLast = costToGoal(startPos, rawDiffs.back().lastPos);
  bool forward = (distToFirst <= distToLast + forwardBias);

  if (!forward) {
    std::reverse(rawDiffs.begin(), rawDiffs.end());
    debug("Processing edits in reverse order (backward)");
  }

  // Adjust indices for sequential application, so edit 2's indices are in buffer after edit 1 is applied
  vector<DiffState> diffStates = Myers::adjustForSequential(rawDiffs);

  int totalEdits = static_cast<int>(diffStates.size());

  // Build intermediate buffer states. [0] = no changes (same as initialLines), [d] = all changes (same as goalLines)
  vector<Lines> linesAfterNEdits = calculateLinesAfterDiffs(
      Lines(initialLines.begin(), initialLines.end()), diffStates, totalEdits);

  vector<EditResult> editResults = calculateEditResults(diffStates);

  // Compute suffix sums of min edit costs for O(1) heuristic lookup
  vector<double> suffixEditCosts = computeSuffixEditCosts(editResults);

  // Position -> editIndex map
  int maxLineSize = 0;
  for (const auto& lines : linesAfterNEdits) {
    maxLineSize = max(maxLineSize, static_cast<int>(lines.size()));
  }
  int maxPosKey = maxLineSize * maxLineLength;
  vector<vector<int>> posToEditIndex = buildPosToEditIndex(diffStates, maxPosKey);

  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);

  vector<Result> res;
  unordered_map<CompositionStateKey, double, CompositionStateKeyHash> costMap;

  priority_queue<CompositionState, vector<CompositionState>, greater<CompositionState>> pq;

  auto exploreNewState = [this, &pq, &costMap, &userEffort, totalEdits, &params](CompositionState&& newState) {
    if(newState.getEffort() > userEffort * params.exploreFactor) {
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
  startingState.updateCost(heuristic(startingState, 0, suffixEditCosts, diffStates, params));
  pq.push(startingState);
  costMap[startingState.getKey()] = startingState.getCost();

  // Main search logic
  while(!pq.empty()) {
    CompositionState s = pq.top();
    pq.pop();
    Position pos = s.getPos();
    int editsCompleted = s.getEditsCompleted();
    Mode mode = s.getMode();

    if(++totalExplored > params.maxSearchDepth) {
      debug("maximum total explored count reached");
      break;
    }

    CompositionStateKey stateKey = s.getKey();
    bool isGoal = (editsCompleted == totalEdits);

    if(isGoal) {
      res.emplace_back(s.getMotionSequence(), s.getRunningEffort().getEffort(config));
      if(res.size() >= static_cast<size_t>(params.maxResults)) {
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

          // Convert buffer position to edit region index (flat index)
          int flatIdx = bufferPosToEditIndex(pos, diff);
          if (flatIdx >= 0 && flatIdx < static_cast<int>(editResult.typeAllResults.size())) {
            const Result& editRes = editResult.typeAllResults[flatIdx];
            if (editRes.isValid()) {
              CompositionState newState = s;

              // After deletion, cursor ends up at start of edit region
              // The edit clears content, then we type the inserted text
              Position newPos = diff.firstPos;

              // Edit results always end in Normal mode (Esc at the end)
              newState.applyEditTransition(editRes.sequences, newPos, Mode::Normal, config);
              newState.updateCost(heuristic(newState, editsCompleted + 1, suffixEditCosts, diffStates, params));
              exploreNewState(std::move(newState));
            }
          }
        }
      }
    }

    // ========== MOVEMENT TRANSITIONS ==========
    // Use MotionOptimizer to find optimal paths to next edit region
    if (editsCompleted < totalEdits) {
      const DiffState& nextEdit = diffStates[editsCompleted];

      // Copy NavContext for motion application
      NavContext navContext = navigationContext;

      // Compute boundary for this sub-search:
      // - Inherit parent boundary's gg/G exclusions
      // - Uses buffer context for the movement search (currentLines)
      MotionBoundary subBoundary(currentLines, nextEdit.firstPos, nextEdit.lastPos, boundary);

      // Use MotionOptimizer to find optimal paths to any position in the edit region
      // Pass only Position and RunningEffort - sub-search computes its own effort/cost fresh
      // RangeResult.keyCost returns delta effort for this movement
      MotionOptimizer movementOptimizer(config);
      vector<RangeResult> movementResults = movementOptimizer.optimizeToRange(
        currentLines,
        pos,
        s.getRunningEffort(),
        nextEdit.firstPos,
        nextEdit.lastPos,
        "", // No user sequence reference for sub-optimization
        navContext,
        false, // allowMultiplePerPosition: only need 1 best path per position
        subBoundary,
        motionToKeys,
        OptimizerParams(clamp(nextEdit.origCharCount(), 1, 10))  // Max results per movement search
      );

      // Create new CompositionStates from movement results
      for (const RangeResult& movResult : movementResults) {
        if (!movResult.isValid()) continue;

        CompositionState newState = s;
        newState.applyMotionResult(movResult.sequences, movResult.endPos, config);
        newState.updateCost(heuristic(newState, editsCompleted, suffixEditCosts, diffStates, params));
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
    for (const Result& r : editRes.typeAllResults) {
      if (r.isValid()) {
        costs.push_back(r.keyCost);
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
                                        const vector<DiffState>& diffStates,
                                        const OptimizerParams& params) const {
  // h(n) = distance to next edit region + suffix sum of edit costs
  // Overshooting (going past the next edit) is penalized more heavily than undershooting.
  // Note: If we're processing edits in reverse order, diffStates was already reversed,
  // so "overshooting" still means pos > nextEdit.lastPos in the processing direction.
  int totalEdits = static_cast<int>(diffStates.size());

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
      // Must backtrack, which is inefficient
      h += overshootPenalty * costToGoal(pos, nextEdit.lastPos);
    }
    // else: inside range, distance = 0
  }

  return params.costWeight * s.getEffort() + h;
}

int CompositionOptimizer::bufferPosToEditIndex(const Position& bufferPos, const DiffState& diff) const {
  // Convert buffer position to flat index within deletedLines
  Lines deleted = diff.deletedLines();
  int editLine = bufferPos.line - diff.origLineStart();

  if (editLine < 0 || editLine >= static_cast<int>(deleted.size())) {
    return -1;
  }

  // Compute flat index: sum of previous line lengths + column
  // For character-level diffs, we need to account for the column offset within the first line
  int flatIndex = 0;

  // If this is a mid-line diff, adjust for the starting column
  if (editLine == 0) {
    // First line of edit region: column is relative to posBegin.col
    flatIndex = bufferPos.col - diff.firstPos.col;
    if (flatIndex < 0) return -1;
  } else {
    // Not first line: sum previous line lengths + current column
    for (int i = 0; i < editLine; i++) {
      flatIndex += static_cast<int>(deleted[i].size());
    }
    flatIndex += bufferPos.col;
  }

  return flatIndex;
}

Position CompositionOptimizer::editIndexToBufferPos(int flatIndex, const DiffState& diff) const {
  // Convert flat index within insertedLines to buffer position
  // The new buffer has insertedLines at diff.newLineStart()
  Lines inserted = diff.insertedLines();

  int remaining = flatIndex;
  for (int i = 0; i < static_cast<int>(inserted.size()); i++) {
    int lineLen = static_cast<int>(inserted[i].size());
    if (remaining < lineLen) {
      // Found the line
      // For first line of mid-line diff, add the starting column offset
      int col = remaining;
      if (i == 0) {
        col += diff.firstPos.col;  // Offset within the line where edit starts
      }
      return Position(diff.newLineStart() + i, col);
    }
    remaining -= lineLen;
  }

  // If we get here, index was at end of last line
  int lastLine = diff.newLineStart() + static_cast<int>(inserted.size()) - 1;
  int lastCol = inserted.empty() ? diff.firstPos.col : static_cast<int>(inserted.back().size());
  if (!inserted.empty() && inserted.size() == 1) {
    lastCol += diff.firstPos.col;
  }
  return Position(lastLine, lastCol);
}

vector<EditResult> CompositionOptimizer::calculateEditResults(const vector<DiffState>& diffStates) {
  EditOptimizer editOptimizer(config);
  vector<EditResult> results;
  results.reserve(diffStates.size());

  for (const DiffState& diff : diffStates) {
    // Run deletion-based approach
    // Pass deletedLines as start, empty/insertedLines as end, and boundary
    EditResult result = editOptimizer.optimizeEdit(
        diff.deletedLines(),
        diff.insertedLines(),
        diff.boundary
    );

    // Check if replacement strategy is applicable and better
    // Replacement works for same-length, single-line transformations
    if (diff.deletedText.size() == diff.insertedText.size() &&
        diff.deletedText.find('\n') == string::npos &&
        diff.insertedText.find('\n') == string::npos &&
        !diff.deletedText.empty() &&
        diff.deletedText != diff.insertedText) {

      vector<Result> replResults;
      int lastReplacementPos = -1;
      tryReplacement(diff.deletedText, diff.insertedText, config, lastReplacementPos, replResults);
      if (!replResults.empty() && replResults[0].isValid()) {
        Result replResult = replResults[0];
        // Replacement starts at column 0, ends in Normal mode
        // Compare to deletion result at position 0 (flat index)
        if (!result.typeAllResults.empty() && result.typeAllResults[0].isValid()) {
          // Deletion cost = delete cost + cost to type all inserted chars
          // (deletion ends in insert mode, we'd type insertedText)
          double typingCost = 0;
          for (char c : diff.insertedText) {
            auto it = CHAR_TO_KEYS.find(c);
            if (it != CHAR_TO_KEYS.end()) {
              typingCost += it->second.size();
            }
          }
          double deletionTotalCost = result.typeAllResults[0].keyCost + typingCost;

          // If replacement is cheaper, update the result
          if (replResult.keyCost < deletionTotalCost) {
            result.typeAllResults[0] = replResult;
            debug("Replacement is cheaper for diff: ", diff.deletedText, " -> ", diff.insertedText,
                  " (", replResult.keyCost, " vs ", deletionTotalCost, ")");
          }
        }
      }
    }

    results.push_back(std::move(result));
  }

  return results;
}

vector<Lines> CompositionOptimizer::calculateLinesAfterDiffs(const Lines& initialLines, const vector<DiffState>& diffStates, int totalEdits) {
  assert(totalEdits == static_cast<int>(diffStates.size()));
  vector<Lines> res(totalEdits + 1);
  res[0] = initialLines;
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

    // For character-level diffs, mark only positions within the actual edit region
    // posBegin and posEnd define the exact character boundaries

    if (diff.isPureInsertion()) {
      // Pure insertion: mark only the insertion point
      int posKey = posToKey(diff.firstPos);
      if (posKey >= 0 && posKey < maxPosKey) {
        posToEditIndex[posKey].push_back(editIdx);
      }
    } else {
      // Deletion or replacement: mark positions from posBegin to posEnd
      // Handle single-line and multi-line cases
      for (int line = diff.firstPos.line; line <= diff.lastPos.line; line++) {
        int startCol = (line == diff.firstPos.line) ? diff.firstPos.col : 0;
        int endCol = (line == diff.lastPos.line) ? diff.lastPos.col : maxLineLength - 1;

        for (int col = startCol; col <= endCol; col++) {
          int posKey = line * maxLineLength + col;
          if (posKey >= 0 && posKey < maxPosKey) {
            posToEditIndex[posKey].push_back(editIdx);
          }
        }
      }
    }
  }

  return posToEditIndex;
}
