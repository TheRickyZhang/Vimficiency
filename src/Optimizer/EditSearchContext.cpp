#include "EditSearchContext.h"
#include "Keyboard/EditToKeys.h"

#include <cstring>

using namespace std;

EditSearchContext::EditSearchContext(const Lines& startLines,
                                     const EditBoundary& boundary,
                                     const OptimizerParams& params,
                                     const Config& config)
    : editBoundary(boundary),
      params(params),
      config(config),
      leftColOffset(static_cast<int>(boundary.prefix().size())),
      rightColOffset(static_cast<int>(boundary.suffix().size())),
      effectiveLines(startLines),
      totalPositions(0) {

  // Build effectiveLines with prefix/suffix baked in
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  if (!pre.empty()) effectiveLines.front().insert(0, pre);
  if (!suf.empty()) effectiveLines.back() += suf;

  // Count total starting positions
  for (const auto& line : startLines) {
    totalPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }
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
  EditStateKey key = state.getKey();
  auto it = costMap.find(key);

  double newCost = state.getCost();
  if (it == costMap.end() || newCost < it->second) {
    costMap[key] = newCost;
    pq.push(std::move(state));
  }
}

void EditSearchContext::initStartingPositions(const Lines& startLines) {
  int startIndex = 0;
  double startCost = heuristic(effectiveLines);

  for (int line = 0; line < static_cast<int>(startLines.size()); line++) {
    int lineCols = startLines[line].empty() ? 1 : static_cast<int>(startLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      int effCol = col + (line == 0 ? leftColOffset : 0);
      pq.push(EditState(effectiveLines, Position(line, effCol), startIndex, startCost));
      startIndex++;
    }
  }
}

pair<int, int> EditSearchContext::computeContentBounds(
    const Lines& lines, const Position& cursor) const {
  int rawLineLen = static_cast<int>(lines[cursor.line].size());
  int contentStart = (cursor.line == 0) ? leftColOffset : 0;
  int contentEnd = rawLineLen;

  if (cursor.line == lines.lastLine() && rightColOffset > 0) {
    contentEnd -= rightColOffset;
  }
  if (contentEnd < 0) {
    cerr << "contentEnd < 0: lines=" << lines << " cursor=" << cursor.line << "," << cursor.col
         << " rawLineLen=" << rawLineLen << " rightColOffset=" << rightColOffset << endl;
  }
  assert(contentEnd >= 0);
  assert(contentStart >= 0);
  return {contentStart, contentEnd};
}

double EditSearchContext::heuristic(const Lines& lines) const {
  int total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (i == 0) lineLen -= leftColOffset;
    if (i == lines.size() - 1) lineLen -= rightColOffset;
    total += max(0, lineLen);
  }
  return static_cast<double>(total);
}

// This is good, but I think we should be able to detect when it stops based on each condition, to let use know whether we have exhausted search (unlikely), reached result threshold, or reached search threshold
bool EditSearchContext::shouldContinue() const {
  return !pq.empty() && resultsFound < totalPositions && iterations < params.maxSearchDepth;
}

// This has potential to be inlined, but worth it for organization
// Nullopt -> there is no next valid state
optional<EditState> EditSearchContext::getNextValidState() {
  while (!pq.empty()) {
    EditState s = pq.top();
    pq.pop();

    // Skip if this state is outdated (we've found a better path)
    EditStateKey key = s.getKey();
    auto it = costMap.find(key);
    if (it != costMap.end() && it->second < s.getCost() - 1e-9)
      continue;

    return s;
  }
  return nullopt;
}

// =============================================================================
// Exploration Helper Methods
// =============================================================================

void EditSearchContext::exploreForwardWordEdits(
    const vector<Edit::ForwardWordEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Position endpoint = VimCore::motionWordEndpoint(
        cursor, lines, true, spec.edgeType, spec.isBig,
        spec.skipCurrent, rightColOffset, editBoundary.hasLinesBelow());

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    onDeletion(Range(cursor, endpoint), spec.cmd, spec.keys);
  }
}

void EditSearchContext::exploreBackwardWordEdits(
    const vector<Edit::BackwardWordEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    // For inclusive backward deletes (dge/dgE), the cursor is part of the
    // deletion range. Check cursor first before computing endpoint.
    if (!spec.isExclusiveAtCursor && inBoundaryRegion(cursor, lines))
      continue;

    Position endpoint = VimCore::motionWordEndpoint(
        cursor, lines, false, spec.edgeType, spec.isBig,
        spec.skipCurrent, leftColOffset, editBoundary.hasLinesAbove());

    if (endpoint == POSITION_OUTSIDE_BOUNDARY || endpoint == cursor)
      continue;

    // db/dB are EXCLUSIVE at cursor: delete backward but NOT the cursor char.
    // Three cases:
    // 1. Cursor not at content start: range ends at cursor.col - 1
    // 2. Cursor at content start AND crossed lines: ends at prev line's last char
    // 3. Cursor at content start AND same line: skip (can't delete backward)
    Range range;
    if (spec.isExclusiveAtCursor) {
      int cursorContentCol = cursor.col - (cursor.line == 0 ? leftColOffset : 0);
      if (cursorContentCol > 0) {
        range = Range(endpoint, Position(cursor.line, cursor.col - 1));
      } else if (endpoint.line < cursor.line) {
        int prevLine = cursor.line - 1;
        int prevLineLen = static_cast<int>(lines[prevLine].size());
        int endCol = prevLineLen > 0 ? prevLineLen - 1 : 0;
        range = Range(endpoint, Position(prevLine, endCol));
      } else {
        continue;
      }
    } else {
      range = Range(endpoint, cursor);
    }

    onDeletion(range, spec.cmd, spec.keys);
  }
}

void EditSearchContext::exploreTextObjectEdits(
    const vector<Edit::TextObjectEditSpec>& specs,
    const Position& cursor, const Lines& lines, DeletionCallback onDeletion) {
  for (const auto& spec : specs) {
    Range range = VimCore::textObjectRange(
        cursor, lines, spec.isInner, spec.isBig,
        leftColOffset, rightColOffset,
        editBoundary.hasLinesAbove(), editBoundary.hasLinesBelow());

    if (range.start == POSITION_OUTSIDE_BOUNDARY || range.end == POSITION_OUTSIDE_BOUNDARY)
      continue;

    onDeletion(range, spec.cmd, spec.keys);
  }
}

void EditSearchContext::exploreHalfLineEdits(
    const vector<Edit::LineEditSpec>& specs,
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    DeletionCallback onDeletion) {
  int lastEditLine = lines.lastLine();

  for (const auto& spec : specs) {
    if (strcmp(spec.cmd, "D") == 0) {
      if (cursor.line == lastEditLine && editBoundary.hasSuffix()) continue;

      int lineLen = static_cast<int>(lines[cursor.line].size());
      int lineContentEnd = lineLen;
      if (cursor.line == lastEditLine && rightColOffset > 0) {
        lineContentEnd -= rightColOffset;
      }
      if (lineContentEnd <= 0) continue;

      int endCol = lineContentEnd - 1;
      if (endCol < cursor.col) continue;
      Range range(cursor, Position(cursor.line, endCol));
      onDeletion(range, spec.cmd, spec.keys);
    } else if (strcmp(spec.cmd, "d0") == 0) {
      if (cursor.line == 0 && editBoundary.hasPrefix()) continue;

      int lineContentStart = (cursor.line == 0) ? leftColOffset : 0;
      if (cursor.col <= lineContentStart) continue;
      Range range(Position(cursor.line, lineContentStart),
                  Position(cursor.line, cursor.col - 1));
      onDeletion(range, spec.cmd, spec.keys);
    } else {
      assert(false && "Unexpected half-line edit command");
    }
  }
}

void EditSearchContext::exploreFullLineEdits(
  const std::vector<Edit::FullLineEditSpec>& specs,
  const Position& cursor, const Lines& lines, LinewiseCallback onLinewise
) {
  if (onLinewise && !isFullLineEditBlocked(lines, cursor)) {
    for (const auto& spec : specs) {
      onLinewise(cursor.line, spec.cmd, spec.keys);
    }
  }
}

void EditSearchContext::exploreCharEdits(
    const Position& cursor, const Lines& lines, int contentStart, int contentEnd,
    int editContentLen, DeletionCallback onDeletion) {
  // x: delete char at cursor
  if (editContentLen > 0 && cursor.col >= contentStart && cursor.col < contentEnd) {
    bool isLastEditChar = (cursor.col == contentEnd - 1);
    bool wouldLandOnBoundary = isLastEditChar && rightColOffset > 0;

    if (wouldLandOnBoundary) {
      if (editContentLen == 1) {
        onDeletion(Range(cursor, cursor), "x", Deletion::CHAR.at("x"));
      }
    } else {
      onDeletion(Range(cursor, cursor), "x", Deletion::CHAR.at("x"));
    }
  }

  // X: delete char before cursor
  if (cursor.col > contentStart) {
    Position before(cursor.line, cursor.col - 1);
    if (!inBoundaryRegion(before, lines)) {
      onDeletion(Range(before, before), "X", Deletion::CHAR.at("X"));
    }
  }
}

// =============================================================================
// Blocking Logic
// =============================================================================

bool EditSearchContext::isFullLineEditBlocked(const Lines& lines, const Position& cursor) const {
  if(cursor.line == 0 && editBoundary.hasPrefix()) {
    return true;
  }
  if(cursor.line == lines.lastLine() && editBoundary.hasSuffix()) {
    return true;
  }

  // // Block if lines above AND below AND multiple lines exist (divergence risk)
  // if (editBoundary.hasLinesAbove() && editBoundary.hasLinesBelow()) {
  //   if (static_cast<int>(lines.size()) > 1) return true;
  // }
  // // Block if cursor not on first line AND lines below (would merge with suffix)
  // if (cursor.line > 0 && editBoundary.hasLinesBelow()) return true;

  return false;
}

// =============================================================================
// Main Exploration Entry Point
// =============================================================================

void EditSearchContext::exploreAllDeletions(const EditState& state,
                                            DeletionCallback onDeletion,
                                            LinewiseCallback onLinewise) {
  const Lines& lines = state.getLines();
  Position cursor = state.getPos();

  // Debug: check for invalid state before computing bounds
  if (cursor.line == lines.lastLine() && rightColOffset > 0) {
    int rawLineLen = static_cast<int>(lines[cursor.line].size());
    if (rawLineLen < rightColOffset) {
      cerr << "INVALID STATE: lines=" << lines << " cursor=" << cursor.line << "," << cursor.col
           << " seq=" << state.getSeq() << endl;
    }
  }

  // Early exit if cursor is in a protected boundary region.
  // This can happen after dd clamps cursor to a position within the suffix region.
  if (inBoundaryRegion(cursor, lines)) {
    return;
  }

  auto [contentStart, contentEnd] = computeContentBounds(lines, cursor);
  int editContentLen = contentEnd - contentStart;

  // Case 1: Empty line -> Explore limited set, since many commands are equivalent
  if (editContentLen <= 0) {
    // Note: inBoundaryRegion check already done above

    // Limited exploration: 
    // (dd == dw == dW)
    exploreFullLineEdits(Edit::EMPTYLINE_FULL_LINE_EDITS, cursor, lines, onLinewise);

    // de/dE
    exploreForwardWordEdits(Edit::EMPTYLINE_FORWARD_WORD_EDITS, cursor, lines, onDeletion);
    // db/dB/dge
    exploreBackwardWordEdits(Edit::EMPTYLINE_BACKWARD_WORD_EDITS, cursor, lines, onDeletion);
    return;
  }

  // Case 2: Normal exploration - full spec sets
  exploreForwardWordEdits(Edit::FORWARD_WORD_EDITS, cursor, lines, onDeletion);
  exploreBackwardWordEdits(Edit::BACKWARD_WORD_EDITS, cursor, lines, onDeletion);
  exploreTextObjectEdits(Edit::TEXT_OBJECT_EDITS, cursor, lines, onDeletion);
  exploreHalfLineEdits(Edit::HALF_LINE_EDITS, cursor, lines, contentStart, contentEnd, onDeletion);
  exploreFullLineEdits(Edit::FULL_LINE_EDITS, cursor, lines, onLinewise);
  exploreCharEdits(cursor, lines, contentStart, contentEnd, editContentLen, onDeletion);
}
