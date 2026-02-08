#include "EditSearchContext.h"
#include "EditExplorer.h"

using namespace std;

EditSearchContext::EditSearchContext(const Lines& initialLines,
                                     const EditBoundary& boundary,
                                     const EditOptimizerParams& params,
                                     const Config& config)
    : editBoundary(boundary),
      params(params),
      config(config),
      leftColOffset(static_cast<int>(boundary.prefix().size())),
      rightColOffset(static_cast<int>(boundary.suffix().size())),
      effectiveLines(initialLines),
      effortWeight(params.effortWeight),
      distanceWeight(params.distanceWeight),
      totalPositions(initialLines.totalPositions()) {

  // Build effectiveLines with prefix/suffix baked in
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  if (!pre.empty()) effectiveLines.front().insert(0, pre);
  if (!suf.empty()) effectiveLines.back() += suf;
}

bool EditSearchContext::inBoundaryRegion(const Position& pos, const Lines& lines) const {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;

  if (pos.line == 0 && pos.col < leftColOffset) return true;
  if (pos.line == lines.lastLine() && rightColOffset > 0 &&
      pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset) {
    return true;
  }
  return false;
}

void EditSearchContext::exploreNewState(EditState&& state) {
  motionsEmitted++;  // Track total operations emitted

  EditStateKey key = state.getKey();
  auto it = costMap.find(key);

  double newCost = state.getCost();
  if (it == costMap.end() || newCost <= it->second) {
    costMap[key] = newCost;
    pq.push(std::move(state));
  }
}

void EditSearchContext::pushGoalState(EditState&& state) {
  motionsEmitted++;
  pq.push(std::move(state));
}

void EditSearchContext::initStartingPositions(const Lines& initialLines) {
  int startIndex = 0;
  // Initial priority: effortWeight * 0 + distanceWeight * distance = distanceWeight * distance
  double startPriority = computePriority(0.0, effectiveLines);

  for (int line = 0; line < static_cast<int>(initialLines.size()); line++) {
    int lineCols = initialLines[line].empty() ? 1 : static_cast<int>(initialLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      int effCol = col + (line == 0 ? leftColOffset : 0);
      // Skip positions past the end of the effective line. This happens when
      // a deleted line is empty but has a prefix: the "cursor" at the prefix
      // boundary (effCol == line length) isn't a valid normal-mode position.
      int effLineLen = static_cast<int>(effectiveLines[line].size());
      if (effLineLen > 0 && effCol >= effLineLen) {
        startIndex++;
        continue;
      }
      pq.push(EditState(effectiveLines, Position(line, effCol), startIndex, startPriority));
      startIndex++;
    }
  }
}

// Returns C++ convention [startCol, endCol) <- EXCLUSIVE END
// of where the edit region is in this line
pair<int, int> EditSearchContext::computeEditBounds(
    const Lines& lines, const Position& cursor) const {
  int rawLineLen = static_cast<int>(lines[cursor.line].size());
  int contentBegin = (cursor.line == 0) ? leftColOffset : 0;
  int contentEnd = rawLineLen;

  if (cursor.line == lines.lastLine() && rightColOffset > 0) {
    contentEnd -= rightColOffset;
  }
  if (contentEnd < 0) {
    cerr << "contentEnd < 0: lines=" << lines << " cursor=" << cursor.line << "," << cursor.col
         << " rawLineLen=" << rawLineLen << " rightColOffset=" << rightColOffset << endl;
  }
  assert(contentEnd >= 0);
  assert(contentBegin >= 0);
  assert(contentEnd >= contentBegin);
  return {contentBegin, contentEnd};
}

double EditSearchContext::distanceHeuristic(const Lines& lines) const {
  int total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (i == 0) lineLen -= leftColOffset;
    if (i == lines.size() - 1) lineLen -= rightColOffset;
    total += max(0, lineLen);
  }
  // Newlines count as 2 (n lines have n-1 newlines)
  total += (static_cast<int>(lines.size()) - 1) * 2;
  return static_cast<double>(total);
}

bool EditSearchContext::shouldContinue() const {
  if (pq.empty()) return false;
  if (uniquePositionsCovered >= totalPositions) return false;
  if (resultsFound >= params.maxResults) return false;
  if (iterations >= params.maxNodesExplored) return false;
  // Safety cap: prevent runaway loops if too many stale nodes
  if (totalPops >= params.maxNodesExplored * SAFETY_MULTIPLIER) return false;
  return true;
}

optional<EditState> EditSearchContext::getNextValidState() {
  while (!pq.empty()) {
    EditState s = pq.top();
    pq.pop();
    totalPops++;  // Track all pops for safety cap

    // Goal states bypass costMap — always valid
    if (s.isGoal()) return s;

    // Skip if this state is outdated (we've found a better path)
    EditStateKey key = s.getKey();
    auto it = costMap.find(key);
    if (it != costMap.end() && it->second < s.getCost() - 1e-9) {
      statesSkipped++;  // Track skipped stale states
      continue;
    }

    return s;
  }
  return nullopt;
}

SearchStats EditSearchContext::getStats() const {
  SearchStats stats;
  stats.nodesExplored = iterations;
  stats.resultsFound = resultsFound;
  stats.queueSizeAtStop = static_cast<int>(pq.size());
  stats.motionsEmitted = motionsEmitted;
  stats.statesSkipped = statesSkipped;

  // Determine stop reason
  if (pq.empty()) {
    stats.stopReason = SearchStopReason::FullyExplored;
  } else if (uniquePositionsCovered >= totalPositions) {
    stats.stopReason = SearchStopReason::AllResultsFound;
  } else if (resultsFound >= params.maxResults) {
    stats.stopReason = SearchStopReason::MaxResultsFound;
  } else if (iterations >= params.maxNodesExplored) {
    stats.stopReason = SearchStopReason::MaxNodesReached;
  }

  return stats;
}

// =============================================================================
// Exploration Delegation to EditExplorer
// =============================================================================

void EditSearchContext::exploreJoinCommands(
    const Position& cursor, const Lines& lines, JoinCallback onJoin) {
  EditExplorer explorer(*this);
  explorer.exploreJoinCommands(cursor, lines, onJoin);
}

void EditSearchContext::exploreAllDeletions(const EditState& state,
                                            DeletionCallback onDeletion,
                                            LinewiseCallback onLinewise,
                                            MotionCallback onMotion,
                                            JoinCallback onJoin) {
  EditExplorer explorer(*this);
  explorer.exploreAllDeletions(state, onDeletion, onLinewise, onMotion, onJoin);
}
