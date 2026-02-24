#include "EditSearchContext.h"
#include "EditExplorer.h"

#include <cassert>
#include <iostream>

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
      bank(config),
      effortWeight(params.effortWeight),
      distanceWeight(params.distanceWeight),
      totalPositions(initialLines.totalPositions()) {

  // Build effectiveLines with prefix/suffix baked in
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  if (!pre.empty()) effectiveLines.front().insert(0, pre);
  if (!suf.empty()) effectiveLines.back() += suf;
}

bool EditSearchContext::inBoundaryRegion(const CursorPos& pos, const Lines& lines) const {
  if (pos.line < 0 || pos.line > lines.lastLine()) return true;

  if (pos.line == 0 && pos.col < leftColOffset) return true;
  if (pos.line == lines.lastLine() && rightColOffset > 0 &&
      pos.col >= static_cast<int>(lines[pos.line].size()) - rightColOffset) {
    return true;
  }
  return false;
}

bool EditSearchContext::exploreBoundaryEscape(const EditState& state,
                                              DeletionCallback onDeletion,
                                              MotionCallback onMotion) {
  const Lines& lines = state.getLines();
  const CursorPos& cursor = state.getPos();

  // Suffix region: cursor on last line, in suffix columns
  if (cursor.line == lines.lastLine() && rightColOffset > 0 &&
      cursor.col + rightColOffset >= static_cast<int>(lines.getSize(cursor.line))) {
    int firstSuffixCol = static_cast<int>(lines.getSize(cursor.line)) - rightColOffset;

    // At exactly the first suffix column, backward word edits are safe:
    // they delete [endpoint, cursor.col-1] which is entirely in content.
    if (onDeletion && cursor.col == firstSuffixCol && firstSuffixCol > 0) {
      EditExplorer explorer(*this);
      explorer.exploreBackwardWordEdits<EdgeType::WordEdge>(
          Edit::BACKWARD_WORDEDGE_EDITS, cursor, lines, onDeletion);
      explorer.exploreBackwardWordEdits<EdgeType::NextEdge>(
          Edit::BACKWARD_NEXTEDGE_EDITS, cursor, lines, onDeletion);
    }

    if (onMotion) {
      if (cursor.col > 0) {
        onMotion(CursorPos(cursor.line, cursor.col - 1),
                 SequenceBinding(KeyedSequence::h, effortFor(KSId::h)));
      }
      if (cursor.line > 0) {
        int newCol = std::min(cursor.targetCol, lines[cursor.line - 1].lastCol());
        onMotion(CursorPos(cursor.line - 1, newCol, cursor.targetCol),
                 SequenceBinding(KeyedSequence::k, effortFor(KSId::k)));
      }
    }
    return true;
  }

  // Prefix region: cursor on line 0, in prefix columns
  if (cursor.line == 0 && cursor.col < leftColOffset) {
    if (onMotion) {
      if (cursor.col < static_cast<int>(lines[0].size()) - 1) {
        onMotion(CursorPos(0, cursor.col + 1),
                 SequenceBinding(KeyedSequence::l, effortFor(KSId::l)));
      }
      if (lines.lastLine() > 0) {
        int newCol = std::min(cursor.targetCol, lines[1].lastCol());
        onMotion(CursorPos(1, newCol, cursor.targetCol),
                 SequenceBinding(KeyedSequence::j, effortFor(KSId::j)));
      }
    }
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

void EditSearchContext::exploreWithDot(EditState&& afterState, const EditState& base,
                                       const SequenceBinding& sourceCmd, double hCost) {
  bool isDot = base.hasLastEdit() &&
               base.getLastEditCount() == sourceCmd.count &&
               base.getLastEditBase() == sourceCmd.base.seq.view();

  if (isDot) {
    afterState.recordSearch(".", effortFor(KSId::Period), effortWeight, hCost, config);
  } else {
    afterState.recordSearch(sourceCmd.count, sourceCmd.base.seq.view(),
                            sourceCmd.effort, effortWeight, hCost, config);
    afterState.setLastEdit(sourceCmd.count, sourceCmd.base.seq.view());
  }
  exploreNewState(std::move(afterState));
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
      pq.push(EditState(effectiveLines, CursorPos(line, effCol), startIndex, startPriority));
      startIndex++;
    }
  }
}

// Returns C++ convention [startCol, endCol) <- EXCLUSIVE END
// of where the edit region is in this line
pair<int, int> EditSearchContext::computeEditBounds(
    const Lines& lines, const CursorPos& cursor) const {
  int rawLineLen = static_cast<int>(lines[cursor.line].size());
  int contentBegin = (cursor.line == 0) ? leftColOffset : 0;
  int contentEnd = rawLineLen;

  if (cursor.line == lines.lastLine() && rightColOffset > 0) {
    contentEnd -= rightColOffset;
  }
  if (contentEnd < 0 || contentEnd < contentBegin) {
    cerr << "computeEditBounds FAIL: contentEnd=" << contentEnd
         << " contentBegin=" << contentBegin
         << " rawLineLen=" << rawLineLen
         << " rightColOffset=" << rightColOffset
         << " leftColOffset=" << leftColOffset
         << " cursor=" << cursor
         << " lines(" << lines.size() << "): " << lines << endl;
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
  stats.exploredStates = exploredStates;

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
    const CursorPos& cursor, const Lines& lines, JoinCallback onJoin) {
  EditExplorer explorer(*this);
  explorer.exploreJoinCommands(cursor, lines, onJoin);
}

void EditSearchContext::exploreAllDeletions(const EditState& state,
                                            DeletionCallback onDeletion,
                                            LinewiseCallback onLinewise,
                                            JoinCallback onJoin) {
  EditExplorer explorer(*this);
  explorer.exploreAllDeletions(state, onDeletion, onLinewise, onJoin);
}

void EditSearchContext::exploreCountedLineEdits(const EditState& state,
                                                 CountedLinewiseCallback cb) {
  EditExplorer explorer(*this);
  explorer.exploreCountedLineEdits(state.getPos(), state.getLines(),
                                   params.minPrefixCount, cb);
}

void EditSearchContext::exploreCountedJoinCommands(const EditState& state,
                                                    CountedJoinCallback cb) {
  EditExplorer explorer(*this);
  explorer.exploreCountedJoinCommands(state.getPos(), state.getLines(),
                                      params.minPrefixCount, cb);
}

void EditSearchContext::exploreCountedWordEdits(const EditState& state,
                                                 DeletionCallback cb) {
  EditExplorer explorer(*this);
  explorer.exploreCountedWordEdits(state.getPos(), state.getLines(),
                                   params.minPrefixCount, cb);
}

void EditSearchContext::exploreCountedCharEdits(const EditState& state,
                                                 DeletionCallback cb) {
  EditExplorer explorer(*this);
  const Lines& lines = state.getLines();
  const CursorPos& cursor = state.getPos();
  auto [contentStart, contentEnd] = computeEditBounds(lines, cursor);
  explorer.exploreCountedCharEdits(cursor, lines, contentStart, contentEnd,
                                   params.minPrefixCount, cb);
}
