#include "CompositionSearchContext.h"

#include <algorithm>
#include <cassert>
#include <limits>

#include "CompositionStepArtifacts.h"
#include "Utils/Debug.h"
#include "Utils/StringUtils.h"

using namespace std;

CompositionSearchContext::CompositionSearchContext(
    const Lines& initialLines,
    const CursorPos& initialPos,
    const Lines& goalLines,
    string_view userSequence,
    const NavContext& navContext,
    const NavBoundary& boundary,
    const CompositionOptimizerParams& params,
    const Config& config)
    : config(config),
      params(params),
      navContext(navContext),
      boundary(boundary),
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

  // Get minimal diff between start and end buffers
  vector<DiffState> rawDiffs = Myers::calculate(
      Lines(initialLines.begin(), initialLines.end()),
      Lines(goalLines.begin(), goalLines.end()));

  // Determine processing direction based on start position relative to edits
  if (!rawDiffs.empty()) {
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
          "del='" + makePrintable(d.deletedText) + "'",
          "ins='" + makePrintable(d.insertedText) + "'");
  }

  // Build intermediate buffer states
  linesAfterNEdits_ = calculateLinesAfterDiffs(initialLines);

  // Pre-compute subset BufferIndex for each edit level (for counted motion exploration).
  // Each index covers the region between the previous edit and current edit + padding,
  // which is where the A* search will query motion landing positions.
  for (int i = 0; i < totalEdits(); i++) {
    const Lines& buf = linesAfterNEdits_[i];
    int bufSize = static_cast<int>(buf.size());

    // Determine the line range the cursor could be in when approaching edit i.
    // For i==0, it's the initial position.
    // For i>0, it's within the region replaced by edit i-1.
    int prevMinLine, prevMaxLine;
    if (i == 0) {
      prevMinLine = prevMaxLine = initialPos.line;
    } else {
      prevMinLine = edits[i - 1].diffState.beginPos.line;
      prevMaxLine = prevMinLine + edits[i - 1].diffState.newLineCount() - 1;
    }

    int currMinLine = edits[i].diffState.beginPos.line;
    int currMaxLine = edits[i].diffState.endPos.line;

    int idxStart = std::max(0, std::min(prevMinLine, currMinLine) - params.navPaddingAbove);
    int idxEnd = std::min(bufSize, std::max(prevMaxLine, currMaxLine) + 1 + params.navPaddingBelow);

    edits[i].bufferIndexStart = idxStart;
    edits[i].bufferIndexEnd = idxEnd;
    edits[i].bufferIndex = BufferIndex(buf.getLineRange(idxStart, idxEnd));
  }

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
  // h(n) = distance to next edit region + suffix sum of edit costs
  // O(1) lookup for remaining edit costs
  double h = suffixEditCosts_[editsCompleted];

  // Add distance to next edit region with asymmetric penalty
  if (editsCompleted < totalEdits()) {
    const DiffState& nextEdit = edits[editsCompleted].diffState;
    CursorPos pos = s.getPos();

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

bool CompositionSearchContext::tryGetBufferIndex(
    int editsCompleted, int motionBeginLine, int motionEndLine,
    const BufferIndex*& outIndex, int& outLineOffset) const {
  outIndex = nullptr;
  outLineOffset = 0;

  if (editsCompleted < 0 || editsCompleted >= totalEdits())
    return false;
  const auto& edit = edits[editsCompleted];
  // Safety: motion search window must be within the indexed range
  if (motionBeginLine < edit.bufferIndexStart || motionEndLine > edit.bufferIndexEnd)
    return false;
  outIndex = &edit.bufferIndex;
  outLineOffset = motionBeginLine - edit.bufferIndexStart;
  return true;
}

CompositionSearchStats CompositionSearchContext::getStats(int resultsFound) const {
  CompositionSearchStats stats;
  SearchStopReason stopReason = SearchStopReason::Unknown;

  if (resultsFound >= params.maxResults) {
    stopReason = SearchStopReason::MaxResultsFound;
  } else if (totalPops >= params.maxNodesPopped) {
    stopReason = SearchStopReason::MaxPopsReached;
  } else if (pq.empty()) {
    stopReason = SearchStopReason::FullyExplored;
  }

  stats.finalize(nodesProcessed,
                        totalPops,
                        resultsFound,
                        static_cast<int>(pq.size()),
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

// Convert (line, col) to flat character index in a Lines buffer.
// Each line is followed by a \n separator (except conceptually the last,
// but flatten() joins with \n so line i occupies [base, base+len] where
// base = sum of (lines[j].size()+1) for j<i).
static int posToFlat(const CursorPos& pos, const Lines& lines) {
  int idx = 0;
  for (int i = 0; i < pos.line && i < static_cast<int>(lines.size()); i++) {
    idx += static_cast<int>(lines[i].size()) + 1;  // +1 for \n
  }
  idx += pos.col;
  return idx;
}

// Convert flat character index back to (line, col) given a Lines buffer.
static CursorPos flatToPos(int flatIdx, const Lines& lines) {
  int remaining = flatIdx;
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (remaining <= lineLen) {
      return CursorPos(i, remaining);
    }
    remaining -= lineLen + 1;  // +1 for \n
  }
  // Past end — clamp to end of last line
  int lastLine = static_cast<int>(lines.size()) - 1;
  return CursorPos(lastLine, static_cast<int>(lines[lastLine].size()));
}

vector<Lines> CompositionSearchContext::calculateLinesAfterDiffs(
    const Lines& initialLines) {
  vector<Lines> result(totalEdits() + 1);
  result[0] = initialLines;

  int cumulativeOffset = 0;

  for (int i = 0; i < totalEdits(); i++) {
    DiffState& diff = edits[i].diffState;
    if (i > 0) {
      // Adjust positions from original-buffer space to intermediate-buffer space.
      // Convert to flat index against original buffer, shift by cumulative delta,
      // then convert back to (line, col) against the current intermediate buffer.
      // Must always adjust when i > 0, even if cumulativeOffset == 0, because
      // earlier diffs may have changed line structure (e.g., \n → space) without
      // changing character count.
      auto adjustPos = [&](const CursorPos& pos) -> CursorPos {
        int flatIdx = posToFlat(pos, initialLines);
        flatIdx += cumulativeOffset;
        return flatToPos(flatIdx, result[i]);
      };

      // Must check before mutating beginPos — hasDeletedContent() compares
      // beginPos != endPos, and the adjusted beginPos may coincidentally equal
      // the unadjusted endPos (see previous_errors.md).
      bool hadDeletedContent = diff.hasDeletedContent();
      diff.beginPos = adjustPos(diff.beginPos);
      if (hadDeletedContent) {
        diff.endPos = adjustPos(diff.endPos);
      } else {
        diff.endPos = diff.beginPos;  // pure insertion
      }
    }

    result[i + 1] = Myers::applyDiffState(diff, result[i]);

    cumulativeOffset += static_cast<int>(diff.insertedText.size())
                      - static_cast<int>(diff.deletedText.size());
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
