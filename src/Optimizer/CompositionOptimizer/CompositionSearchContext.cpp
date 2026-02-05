#include "CompositionSearchContext.h"

#include "Keyboard/MotionToKeys.h"
#include "State/RunningEffort.h"
#include "Utils/Debug.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <tuple>

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
    const Config& config)
    : config(config),
      params(params),
      navContext(navContext),
      boundary(boundary),
      motionToKeys(rawMotionToKeys),
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
    bool processForward = (distToFirst <= distToLast + 1.0);  // Slight bias toward forward

    if (!processForward) {
      std::reverse(rawDiffs.begin(), rawDiffs.end());
      debug("Processing edits in reverse order (backward)");
    }

    diffStates = std::move(rawDiffs);
  }

  // Build intermediate buffer states
  linesAfterNEdits = calculateLinesAfterDiffs(initialLines);

  // Solve each edit region
  editResults = calculateEditResults();

  // Compute text object contexts for shortcuts
  textObjectContexts = computeTextObjectContexts();

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

void CompositionSearchContext::exploreEditTransition(
    const CompositionState& current,
    const Sequence& editSequence,
    const Position& goalPos,
    int editsAfter) {
  CompositionState newState = current.afterEditTransition(
      editSequence, goalPos, Mode::Normal, config);
  newState.setCost(heuristic(newState, editsAfter));
  exploreNewState(std::move(newState));
}

void CompositionSearchContext::exploreMotionTransition(
    const CompositionState& current,
    const Sequence& moveSequence,
    const Position& goalPos,
    int editsCompleted) {
  CompositionState newState = current.afterMotionResult(
      moveSequence, goalPos, config);
  newState.setCost(heuristic(newState, editsCompleted));
  exploreNewState(std::move(newState));
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
    for (const Result& r : editRes.results) {
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
      result.results[0] = Result(Sequence(seq), effort);

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

// Convert (line, col) to flat character index in a Lines buffer.
// Each line is followed by a \n separator (except conceptually the last,
// but flatten() joins with \n so line i occupies [base, base+len] where
// base = sum of (lines[j].size()+1) for j<i).
static int posToFlat(const Position& pos, const Lines& lines) {
  int idx = 0;
  for (int i = 0; i < pos.line && i < static_cast<int>(lines.size()); i++) {
    idx += static_cast<int>(lines[i].size()) + 1;  // +1 for \n
  }
  idx += pos.col;
  return idx;
}

// Convert flat character index back to (line, col) given a Lines buffer.
static Position flatToPos(int flatIdx, const Lines& lines) {
  int remaining = flatIdx;
  for (int i = 0; i < static_cast<int>(lines.size()); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (remaining <= lineLen) {
      return Position(i, remaining);
    }
    remaining -= lineLen + 1;  // +1 for \n
  }
  // Past end — clamp to end of last line
  int lastLine = static_cast<int>(lines.size()) - 1;
  return Position(lastLine, static_cast<int>(lines[lastLine].size()));
}

vector<Lines> CompositionSearchContext::calculateLinesAfterDiffs(
    const Lines& initialLines) {
  assert(totalEdits == static_cast<int>(diffStates.size()));
  vector<Lines> result(totalEdits + 1);
  result[0] = initialLines;

  int cumulativeOffset = 0;

  for (int i = 0; i < totalEdits; i++) {
    if (cumulativeOffset != 0) {
      // Adjust positions from original-buffer space to intermediate-buffer space.
      // Convert to flat index against original buffer, shift by cumulative delta,
      // then convert back to (line, col) against the current intermediate buffer.
      auto adjustPos = [&](const Position& pos) -> Position {
        int flatIdx = posToFlat(pos, initialLines);
        flatIdx += cumulativeOffset;
        return flatToPos(flatIdx, result[i]);
      };

      diffStates[i].beginPos = adjustPos(diffStates[i].beginPos);
      if (diffStates[i].hasDeletedContent()) {
        diffStates[i].endPos = adjustPos(diffStates[i].endPos);
      } else {
        diffStates[i].endPos = diffStates[i].beginPos;  // pure insertion
      }
    }

    result[i + 1] = Myers::applyDiffState(diffStates[i], result[i]);

    cumulativeOffset += static_cast<int>(diffStates[i].insertedText.size())
                      - static_cast<int>(diffStates[i].deletedText.size());
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
void scanQuotesForEdit(TextObjectContext& ctx, const string& line,
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
void scanBracketsForEdit(TextObjectContext& ctx, const string& line,
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

vector<TextObjectContext> CompositionSearchContext::computeTextObjectContexts() const {
  vector<TextObjectContext> contexts;
  contexts.resize(totalEdits);

  for (int i = 0; i < totalEdits; i++) {
    const DiffState& diff = diffStates[i];
    TextObjectContext& ctx = contexts[i];

    // Skip pure insertions (no content to match against)
    if (diff.isPureInsertion()) continue;

    // Skip multi-line edits (quotes are single-line only, brackets need more work)
    if (diff.beginPos.line != diff.endPos.line) continue;

    const Lines& buffer = linesAfterNEdits[i];
    if (diff.beginPos.line >= static_cast<int>(buffer.size())) continue;

    const string& line = buffer[diff.beginPos.line];
    ctx.line = diff.beginPos.line;

    // endPos.col is half-open (one past last deleted char)
    scanQuotesForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
    scanBracketsForEdit(ctx, line, diff.beginPos.col, diff.endPos.col);
  }

  return contexts;
}


