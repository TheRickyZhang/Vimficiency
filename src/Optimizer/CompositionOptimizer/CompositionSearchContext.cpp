#include "CompositionSearchContext.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <tuple>

#include "Optimizer/BuildTypedCommands.h"

#include "Keyboard/KeyedSequence.h"
#include "Effort/RunningEffort.h"
#include "Utils/Debug.h"
#include "Utils/StringUtils.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

CompositionSearchContext::CompositionSearchContext(
    const Lines& initialLines,
    const CursorPos& initialPos,
    const Lines& goalLines,
    string_view userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
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

    int idxStart = std::max(0, std::min(prevMinLine, currMinLine) - params.motionPaddingAbove);
    int idxEnd = std::min(bufSize, std::max(prevMaxLine, currMaxLine) + 1 + params.motionPaddingBelow);

    edits[i].bufferIndexStart = idxStart;
    edits[i].bufferIndexEnd = idxEnd;
    edits[i].bufferIndex = BufferIndex(buf.getLineRange(idxStart, idxEnd));
  }

  // Solve each edit region
  calculateEditResults();

  debug("--- edit results ---");
  for (int i = 0; i < totalEdits(); i++) {
    const auto& er = edits[i].editResult;
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

void CompositionSearchContext::exploreEditTransition(
    const CompositionState& current,
    const Sequence& editSequence,
    const CursorPos& goalPos,
    int editsAfter) {
  CompositionState newState = current.afterEditTransition(
      editSequence, goalPos, Mode::Normal, config);
  newState.setCost(heuristic(newState, editsAfter));
  exploreNewState(std::move(newState));
}

void CompositionSearchContext::exploreMotionTransition(
    const CompositionState& current,
    const Sequence& moveSequence,
    const CursorPos& goalPos,
    int editsCompleted) {
  CompositionState newState = current.afterMotionResult(
      moveSequence, goalPos, config);
  newState.setCost(heuristic(newState, editsCompleted));
  exploreNewState(std::move(newState));
}

void CompositionSearchContext::exploreNewState(CompositionState&& newState) {
  if (newState.getEffort() > maxEffort) {
    debug("  pruned (effort", newState.getEffort(), ">", maxEffort, "):",
          "\"" + newState.getSequence().str() + "\"");
    return;
  }

  double newCost = newState.getCost();
  const CompositionStateKey newKey = newState.getKey();
  auto it = costMap.find(newKey);

  if (it == costMap.end()) {
    // Don't cache goal states (we want multiple results)
    if (newState.getEditsCompleted() != totalEdits()) {
      costMap.emplace(newKey, newCost);
    }
    pq.push(std::move(newState));
  } else if (newCost <= it->second) {
    it->second = newCost;
    pq.push(std::move(newState));
  } else {
    debug("  not enqueued (cost", newCost, ">=", it->second, "):",
          "\"" + newState.getSequence().str() + "\"");
  }
}

CompositionSearchStats CompositionSearchContext::getStats(int resultsFound) const {
  CompositionSearchStats stats;
  stats.nodesExplored = nodesProcessed;
  stats.totalPops = totalPops;
  stats.resultsFound = resultsFound;
  stats.queueSizeAtStop = static_cast<int>(pq.size());
  stats.statesSkipped = statesSkipped;
  stats.motionNodesExplored = motionNodesExplored;
  stats.editNodesExplored = editNodesExplored;
  // exploredStates not copied — composition uses CompositionExploredState,
  // accessed directly via ctx.exploredStates in ExplorationCollector

  if (resultsFound >= params.maxResults) {
    stats.stopReason = SearchStopReason::MaxResultsFound;
  } else if (totalPops >= params.maxTotalPops) {
    stats.stopReason = SearchStopReason::MaxPopsReached;
  } else if (pq.empty()) {
    stats.stopReason = SearchStopReason::FullyExplored;
  }

  return stats;
}

vector<double> CompositionSearchContext::computeSuffixEditCosts() const {
  int n = totalEdits();
  vector<double> suffixCosts(n + 1, 0.0);

  for (int i = n - 1; i >= 0; i--) {
    double medianCost;

    // All edits now have EditResult (including pure insertions)
    const auto& editRes = edits[i].editResult;
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

// Helper: compute cursor position after insertion
// Cursor ends on last char of inserted text, or stays at insertPos if empty
static CursorPos computeInsertEndPos(CursorPos insertPos, const string& insertedText) {
  if (insertedText.empty()) {
    return insertPos;
  }
  Lines inserted = Lines::unflatten(insertedText);
  if (inserted.size() == 1) {
    // Single line: cursor at last char
    int endCol = insertPos.col + static_cast<int>(inserted[0].size()) - 1;
    return CursorPos(insertPos.line, max(0, endCol));
  } else {
    // Multi-line: cursor at last char of last line
    int lastLine = insertPos.line + static_cast<int>(inserted.size()) - 1;
    int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
    return CursorPos(lastLine, lastCol);
  }
}

void CompositionSearchContext::calculateEditResults() {
  EditOptimizer editOptimizer(config);

  for (size_t i = 0; i < edits.size(); i++) {
    const DiffState& diff = edits[i].diffState;
    // Handle pure insertions: create single-entry EditResult with precomputed "i + text + <Esc>"
    if (diff.isPureInsertion()) {
      // Build insert sequence: i + typed content + <Esc>
      Lines insertLines = Lines::unflatten(diff.insertedText);
      KeyedSequence full = KeyedSequence::i;
      full += buildTypedCommands(insertLines);
      RunningEffort runningEffort(full.keys, config);
      double effort = runningEffort.getEffort(config);

      std::vector<std::vector<Result>> insertResultsByStart(1);
      insertResultsByStart[0].emplace_back(std::move(full.seq), effort);

      CursorPos goalPos = computeInsertEndPos(diff.beginPos, diff.insertedText);
      // Use single-char Lines for lineBaseIndex computation (insertion point has no content)
      Lines singlePoint = {""};
      edits[i].editResult = EditResult(std::move(insertResultsByStart), {}, singlePoint,
                                       diff.beginPos.line, diff.beginPos.col, goalPos);
      continue;
    }

    if (diff.isPureDeletion()) {
      edits[i].editResult = editOptimizer.optimizePureDeletion(
          diff.deletedLines(), diff.boundary,
          EditOptimizerParams{}
              .withMinCountRepeat(params.minPrefixCount)
              .withMaxCountRepeat(params.maxPrefixCount)
              .withMaxMultiplePerStartPosition(params.maxEditResultsPerPosition)
              .withTrackExploredStates(params.trackExploredStates),
          diff.beginPos.line, diff.beginPos.col, diff.beginPos);
      editNodesExplored += edits[i].editResult.getStats().nodesExplored;
      continue;
    }

    // Compute cursor position after edit completes
    // After change + typed text + <Esc>, cursor is at last char of inserted text
    CursorPos goalPos;
    const Lines& inserted = diff.insertedLines();
    if (inserted.empty() || (inserted.size() == 1 && inserted[0].empty())) {
      // Pure deletion or empty insertion: cursor at start of edit region
      goalPos = diff.beginPos;
    } else if (inserted.size() == 1) {
      // Single line: cursor at last char of inserted text
      goalPos = CursorPos(diff.beginPos.line,
                         diff.beginPos.col + static_cast<int>(inserted[0].size()) - 1);
    } else {
      // Multi-line: cursor at last char of last inserted line
      int lastLine = diff.beginPos.line + static_cast<int>(inserted.size()) - 1;
      int lastCol = inserted.back().empty() ? 0 : static_cast<int>(inserted.back().size()) - 1;
      goalPos = CursorPos(lastLine, lastCol);
    }

    EditResult optResult = editOptimizer.optimizeEdit(
        diff.deletedLines(), diff.insertedLines(), diff.boundary,
        EditOptimizerParams{}
            .withMinCountRepeat(params.minPrefixCount)
            .withMaxCountRepeat(params.maxPrefixCount)
            .withMaxMultiplePerStartPosition(params.maxEditResultsPerPosition)
            .withTrackExploredStates(params.trackExploredStates),
        diff.beginPos.line, diff.beginPos.col, goalPos);
    editNodesExplored += optResult.getStats().nodesExplored;
    edits[i].editResult = std::move(optResult);
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

// =============================================================================
// Text Object Context Computation
// =============================================================================

namespace {

// Helper: find quote pair matching [beginCol, endCol) on a single line
// Returns {openCol, closeCol, isAround} where isAround indicates if this is an "around" match
// For "inner": open < beginCol, inner region [open+1, close) == [beginCol, endCol)
// For "around": around region [open, close+1) == [beginCol, endCol)
tuple<int, int, bool> findMatchingQuotePair(const string& line, int beginCol, int endCol, char quote) {
  // Collect all quote positions
  vector<int> quotePositions;
  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] == quote) {
      quotePositions.push_back(i);
    }
  }

  // Check each pair for inner or around match
  // Quotes pair in order: 0-1, 2-3, 4-5, etc.
  for (size_t i = 0; i + 1 < quotePositions.size(); i += 2) {
    int open = quotePositions[i];
    int close = quotePositions[i + 1];

    // Check inner match: [open+1, close) == [beginCol, endCol)
    if (open + 1 == beginCol && close == endCol) {
      return {open, close, false};  // inner match
    }

    // Check around match: [open, close+1) == [beginCol, endCol)
    if (open == beginCol && close + 1 == endCol) {
      return {open, close, true};  // around match
    }
  }
  return {-1, -1, false};
}

// Helper: find bracket pair matching [beginCol, endCol) on a single line
// Returns {openCol, closeCol, isAround} where isAround indicates if this is an "around" match
// For "inner": inner region [open+1, close) == [beginCol, endCol)
// For "around": around region [open, close+1) == [beginCol, endCol)
tuple<int, int, bool> findMatchingBracketPair(const string& line, int beginCol, int endCol,
                                               char open, char close) {
  int bestOpen = -1;
  int bestClose = -1;
  bool bestIsAround = false;

  // Find all bracket pairs and check for matches
  vector<int> openStack;
  for (int i = 0; i < static_cast<int>(line.size()); i++) {
    if (line[i] == open) {
      openStack.push_back(i);
    } else if (line[i] == close && !openStack.empty()) {
      int openPos = openStack.back();
      openStack.pop_back();

      // Check inner match: [openPos+1, i) == [beginCol, endCol)
      if (openPos + 1 == beginCol && i == endCol) {
        // Prefer innermost (larger openPos)
        if (bestOpen == -1 || openPos > bestOpen) {
          bestOpen = openPos;
          bestClose = i;
          bestIsAround = false;
        }
      }

      // Check around match: [openPos, i+1) == [beginCol, endCol)
      if (openPos == beginCol && i + 1 == endCol) {
        if (bestOpen == -1 || openPos > bestOpen) {
          bestOpen = openPos;
          bestClose = i;
          bestIsAround = true;
        }
      }
    }
  }

  return {bestOpen, bestClose, bestIsAround};
}

// Scan quotes for a single edit, populating context
void scanQuotesForEdit(BracketQuoteContext& ctx, const string& line,
                       int beginCol, int endCol) {
  int lineLen = static_cast<int>(line.size());
  if (lineLen == 0) return;

  ctx.validQuoteMask.resize(lineLen);

  for (char quote : {'"', '\'', '`'}) {
    auto [openCol, closeCol, isAround] = findMatchingQuotePair(line, beginCol, endCol, quote);
    if (openCol == -1) continue;
    if (closeCol >= lineLen) continue;

    if (isAround) {
      ctx.useAroundQuote.add(quote);
    }

    // Find first quote of this type on line
    int firstQuoteOfType = -1;
    for (int col = 0; col < lineLen; col++) {
      if (line[col] == quote) {
        firstQuoteOfType = col;
        break;
      }
    }

    if (firstQuoteOfType == openCol) {
      // First pair: Neovim forward-searches from any position before closeCol
      // and finds this pair. Positions inside the pair also use this pair.
      for (int col = 0; col <= closeCol; col++) {
        ctx.validQuoteMask[col].add(quote);
      }
    } else {
      // Subsequent pair: only positions ON or INSIDE the pair are valid.
      // From before the pair, ci" forward-searches and hits an earlier pair.
      for (int col = openCol; col <= closeCol; col++) {
        ctx.validQuoteMask[col].add(quote);
      }
    }
  }
}

// Scan brackets for a single edit, populating context
void scanBracketsForEdit(BracketQuoteContext& ctx, const string& line,
                         int beginCol, int endCol) {
  int lineLen = static_cast<int>(line.size());
  if (lineLen == 0) return;

  ctx.validBracketMask.resize(lineLen);

  for (auto [open, close] : vector<pair<char, char>>{{'(', ')'}, {'[', ']'}, {'{', '}'}, {'<', '>'}}) {
    auto [openCol, closeCol, isAround] = findMatchingBracketPair(line, beginCol, endCol, open, close);
    if (openCol == -1) continue;
    if (openCol >= lineLen || closeCol >= lineLen) continue;

    if (isAround) {
      ctx.useAroundBracket.add(open);
    }

    // Precompute: first opening bracket of this type at or after each column.
    // Used to check if forward-search from a position reaches the target pair.
    vector<int> firstOpenForward(lineLen, -1);
    {
      int nextOpen = -1;
      for (int col = lineLen - 1; col >= 0; col--) {
        if (line[col] == open) nextOpen = col;
        firstOpenForward[col] = nextOpen;
      }
    }

    // Mark valid positions:
    // 1. Inside the target pair (openCol..closeCol): always valid
    //    (Neovim uses innermost pair containing cursor)
    // 2. Before the pair at balance=0: valid only if forward-search
    //    reaches the target pair's openCol (no earlier bracket in the way)
    int balance = 0;
    for (int col = 0; col < lineLen; col++) {
      bool insidePair = (col >= openCol && col <= closeCol);
      bool forwardReachesPair = (col < openCol && balance == 0 &&
                                 firstOpenForward[col] == openCol);
      if (insidePair || forwardReachesPair) {
        ctx.validBracketMask[col].add(open);
      }
      if (line[col] == open) balance++;
      else if (line[col] == close) balance--;
    }
  }
}

} // anonymous namespace

void CompositionSearchContext::computeTextObjectContexts() {
  for (int i = 0; i < totalEdits(); i++) {
    const DiffState& diff = edits[i].diffState;
    BracketQuoteContext& ctx = edits[i].bracketQuoteContext;

    // Skip pure insertions (no content to match against)
    if (diff.isPureInsertion()) continue;

    // Skip multi-line edits (quotes are single-line only, brackets need more work)
    if (diff.beginPos.line != diff.endPos.line) continue;

    const Lines& buffer = linesAfterNEdits_[i];
    if (diff.beginPos.line >= static_cast<int>(buffer.size())) continue;

    const string& line = buffer[diff.beginPos.line];
    ctx.line = diff.beginPos.line;

    // endPos.col is half-open (one past last deleted char)
    scanQuotesForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
    scanBracketsForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
  }
}

// =============================================================================
// J (Join Lines) Plan Computation
// =============================================================================

namespace {

// Simulate Vim's J command on a set of source lines, returning the joined result
// and the cursor column after each J. Uses VimEditUtils::joinLines semantics.
struct JoinSimulation {
  string joinedLine;          // Result after all J's
  vector<int> cursorCols;     // Cursor col after each J (size = numJoins)

  // Simulate J over half-open source line interval [begin, end).
  static JoinSimulation simulate(const Lines& srcLines, int begin, int end) {
    JoinSimulation sim;
    Lines workLines(srcLines.begin() + begin, srcLines.begin() + end);
    CursorPos pos(0, 0);

    for (int l = begin + 1; l < end; l++) {
      VimCore::joinLines(workLines, pos, /*addSpace=*/true);
      sim.cursorCols.push_back(pos.col);
    }
    sim.joinedLine = workLines[0];
    return sim;
  }
};

// Compute common prefix length between two strings
int commonPrefixLen(string_view a, string_view b) {
  int n = static_cast<int>(min(a.size(), b.size()));
  for (int i = 0; i < n; i++) {
    if (a[i] != b[i]) return i;
  }
  return n;
}

// Compute common suffix length between two strings (non-overlapping with prefix)
int commonSuffixLen(string_view a, string_view b, int prefixLen) {
  int la = static_cast<int>(a.size());
  int lb = static_cast<int>(b.size());
  int maxSuffix = min(la, lb) - prefixLen;
  int count = 0;
  for (int i = 0; i < maxSuffix; i++) {
    if (a[la - 1 - i] != b[lb - 1 - i]) break;
    count++;
  }
  return count;
}

} // anonymous namespace

void CompositionSearchContext::computeJoinPlans() {
  EditOptimizer editOptimizer(config);

  for (int i = 0; i < totalEdits(); i++) {
    const DiffState& diff = edits[i].diffState;

    // Quick reject: pure insertions can't use J
    if (diff.isPureInsertion()) continue;

    // Get the full buffer lines spanning the diff region (not just the changed text).
    // J operates on complete buffer lines, including the unchanged prefix/suffix.
    const Lines& buffer = linesAfterNEdits_[i];
    int srcFirstLine = diff.beginPos.line;
    // endPos is half-open. The diff spans from beginPos to endPos.
    // If endPos.col == 0, the deletion ends at the start of endPos.line, meaning
    // the last source line with deleted content is endPos.line - 1. But for J,
    // we still need endPos.line if the newline between lines was deleted (which
    // brought content from endPos.line onto the joined result).
    // Use the number of lines in deletedLines() to determine the span.
    Lines delLines = diff.deletedLines();
    int srcEndLine = srcFirstLine + static_cast<int>(delLines.size());
    if (srcEndLine > static_cast<int>(buffer.size())) continue;

    int N = srcEndLine - srcFirstLine;  // number of source lines in buffer

    // Get corresponding target lines from the post-diff buffer
    const Lines& bufferAfter = linesAfterNEdits_[i + 1];
    // Determine target line count: same first line, but fewer lines after diff applied
    Lines tgtLines = diff.insertedLines();
    int M = static_cast<int>(tgtLines.size());

    // For M>1 or M==1, we need the full target lines (with prefix/suffix reattached)
    // Reconstruct full target lines by attaching the prefix and suffix
    const string& prefix = diff.boundary.prefix();
    const string& suffix = diff.boundary.suffix();

    // Build full source lines (from buffer)
    Lines srcLines = buffer.getLineRange(srcFirstLine, srcEndLine);

    // Build full target lines (reattach prefix to first line, suffix to last line)
    Lines fullTgtLines;
    for (int t = 0; t < M; t++) {
      string line = tgtLines[t];
      if (t == 0 && M == 1) {
        line = prefix + line + suffix;
      } else if (t == 0) {
        line = prefix + line;
      } else if (t == M - 1) {
        line = line + suffix;
      }
      fullTgtLines.push_back(std::move(line));
    }

    // Need more source lines than target lines for J to be useful
    if (N <= M) continue;

    debug("  joinPlan[" + to_string(i) + "]: considering N=" + to_string(N) +
          " -> M=" + to_string(M) + " srcLines=" + to_string(srcFirstLine) +
          ".." + to_string(srcEndLine - 1));

    // === Find best partition of N source lines into M groups ===
    // partition[k] = {begin, end} half-open indices into srcLines (0-based)
    vector<pair<int, int>> partition(M);

    if (M == 1) {
      partition[0] = {0, N};
    } else {
      // Prefix sums of source line lengths for O(1) joined length computation
      // Note: this uses a simplified model; actual J join adds spaces and strips ws.
      // For partition finding, line lengths are a good enough approximation.
      vector<int> prefLen(N + 1, 0);
      for (int s = 0; s < N; s++) {
        prefLen[s + 1] = prefLen[s] + static_cast<int>(srcLines[s].size());
      }
      auto joinedLen = [&](int a, int b) -> int {
        return prefLen[b + 1] - prefLen[a] + (b - a);
      };

      constexpr int INF = 1000000;
      vector<vector<int>> dp(N + 1, vector<int>(M + 1, INF));
      vector<vector<int>> choice(N + 1, vector<int>(M + 1, -1));
      dp[N][M] = 0;

      for (int t = M - 1; t >= 0; t--) {
        int tgtLen = static_cast<int>(fullTgtLines[t].size());
        for (int s = N - (M - t); s >= t; s--) {
          for (int k = s; k <= N - (M - t); k++) {
            int groupCost = abs(joinedLen(s, k) - tgtLen);
            int total = groupCost + dp[k + 1][t + 1];
            if (total < dp[s][t]) {
              dp[s][t] = total;
              choice[s][t] = k;
            }
          }
        }
      }

      int s = 0;
      for (int t = 0; t < M; t++) {
        int k = choice[s][t];
        partition[t] = {s, k + 1};
        s = k + 1;
      }
    }

    // === Check match quality for each group ===
    bool viable = true;
    for (int g = 0; g < M; g++) {
      auto [begin, end] = partition[g];
      if (end - begin <= 1) continue;

      auto sim = JoinSimulation::simulate(srcLines, begin, end);
      int cpLen = commonPrefixLen(sim.joinedLine, fullTgtLines[g]);
      int csLen = commonSuffixLen(sim.joinedLine, fullTgtLines[g], cpLen);
      int commonLen = cpLen + csLen;
      int maxLen = max(static_cast<int>(sim.joinedLine.size()),
                       static_cast<int>(fullTgtLines[g].size()));
      double matchRatio = maxLen > 0 ? static_cast<double>(commonLen) / maxLen : 1.0;
      if (matchRatio < 0.3) {
        debug("    group " + to_string(g) + " matchRatio=" + to_string(matchRatio) +
              " below threshold, skipping J plan");
        viable = false;
        break;
      }
    }
    if (!viable) continue;

    // === Per-group processing: simulate J, compute residual ===
    Sequence fullSeq;
    CursorPos lastGoalPos(0, 0);
    bool failed = false;

    for (int g = 0; g < M; g++) {
      auto [begin, end] = partition[g];
      int numJoins = end - begin - 1;

      if (g > 0) {
        fullSeq.append("j");
      }

      for (int j = 0; j < numJoins; j++) {
        fullSeq.append("J");
      }

      // Simulate J to get joined content and cursor position
      auto sim = JoinSimulation::simulate(srcLines, begin, end);
      int cursorCol = numJoins > 0 ? sim.cursorCols.back() : 0;

      // Check if residual edit is needed (joined line vs full target line)
      if (sim.joinedLine != fullTgtLines[g]) {
        // Run EditOptimizer on the full joined line vs full target line.
        // The full line is the edit region (no prefix/suffix beyond what's
        // already on the line). This avoids cursor-position misalignment issues.
        Lines residualInitial = {sim.joinedLine};
        // The entire joined line is the edit region (single line, no prefix/suffix)
        EditBoundary groupBoundary;

        CursorPos residualGoalPos(0,
            fullTgtLines[g].empty() ? 0
            : static_cast<int>(fullTgtLines[g].size()) - 1);
        EditOptimizerParams residualParams =
            EditOptimizerParams{}
                .withMinCountRepeat(params.minPrefixCount)
                .withMaxCountRepeat(params.maxPrefixCount);

        EditResult residualResult = [&]() -> EditResult {
          if (fullTgtLines[g].empty()) {
            return editOptimizer.optimizePureDeletion(
                residualInitial, groupBoundary, residualParams,
                0, 0, residualGoalPos);
          }
          Lines residualGoal = {fullTgtLines[g]};
          return editOptimizer.optimizeEdit(
              residualInitial, residualGoal, groupBoundary, residualParams,
              0, 0, residualGoalPos);
        }();
        editNodesExplored += residualResult.getStats().nodesExplored;

        // Look up result at cursor position after J
        const Result* res = residualResult.resultAt(0, cursorCol);
        if (!res) {
          debug("    group " + to_string(g) + " no EditResult at cursorCol=" +
                to_string(cursorCol) + ", skipping J plan");
          failed = true;
          break;
        }

        fullSeq.append(res->getSequence().view());
        lastGoalPos = residualResult.getGoalPos();
      } else {
        lastGoalPos = CursorPos(0, cursorCol);
      }
    }

    if (failed) continue;

    // Compute goalPos in buffer coordinates
    CursorPos goalPos;
    if (M == 1) {
      goalPos = CursorPos(diff.beginPos.line, lastGoalPos.col);
    } else {
      int bufferLine = diff.beginPos.line + M - 1;
      goalPos = CursorPos(bufferLine, lastGoalPos.col);
    }

    double effort = getEffort(fullSeq.view(), config);

    edits[i].joinPlan = JoinPlan{
      .sequence = std::move(fullSeq),
      .goalPos = goalPos,
      .effort = effort,
      .entryLine = diff.beginPos.line
    };
  }
}
