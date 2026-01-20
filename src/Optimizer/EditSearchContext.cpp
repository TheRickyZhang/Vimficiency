#include "EditSearchContext.h"
#include "Keyboard/EditToKeys.h"

using namespace std;

// Helper to compute character count for heuristic
static int countChars(const Lines& lines) {
  int count = 0;
  for (const auto& line : lines) {
    count += static_cast<int>(line.size());
  }
  return count;
}

EditSearchContext::EditSearchContext(const Lines& startLines,
                                     const EditBoundary& boundary,
                                     const OptimizerParams& params,
                                     const Config& config)
    : editBoundary(boundary),
      params(params),
      config(config),
      pre(boundary.prefix()),
      suf(boundary.suffix()),
      preSuf(pre + suf),
      leftColOffset(static_cast<int>(pre.size())),
      rightColOffset(static_cast<int>(suf.size())),
      effectiveLines(startLines),
      totalPositions(0) {

  // Build effectiveLines with prefix/suffix baked in
  if (!pre.empty()) effectiveLines.front().insert(0, pre);
  if (!suf.empty()) effectiveLines.back() += suf;

  // Count total starting positions
  for (const auto& line : startLines) {
    totalPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }
}

bool EditSearchContext::inBoundaryRegion(const Position& pos, const Lines& lines) const {
  // Lines invariant: buffer always has at least one line
  int lastLine = static_cast<int>(lines.size()) - 1;

  if (pos.line < 0 || pos.line > lastLine) return true;

  if (pos.line == 0 && pos.col < leftColOffset) return true;
  if (pos.line == lastLine && rightColOffset > 0 &&
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
  double startCost = countChars(startLines);  // Simple heuristic

  for (int line = 0; line < static_cast<int>(startLines.size()); line++) {
    int lineCols = startLines[line].empty() ? 1 : static_cast<int>(startLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      int effCol = col + (line == 0 ? leftColOffset : 0);
      pq.push(EditState(effectiveLines, Position(line, effCol), startIndex, startCost));
      startIndex++;
    }
  }
}

tuple<int, int, int, int> EditSearchContext::computeContentBounds(
    const Lines& lines, const Position& cursor) const {
  int rawLineLen = static_cast<int>(lines[cursor.line].size());
  int contentStart = (cursor.line == 0) ? leftColOffset : 0;
  int contentEnd = rawLineLen;
  int lastEditLine = static_cast<int>(lines.size()) - 1;

  if (cursor.line == lastEditLine && rightColOffset > 0) {
    contentEnd -= rightColOffset;
  }
  int editContentLen = max(0, contentEnd - contentStart);

  return {contentStart, contentEnd, editContentLen, lastEditLine};
}

double EditSearchContext::computeRemainingHeuristic(const Lines& lines) const {
  int total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    int lineLen = static_cast<int>(lines[i].size());
    if (i == 0) lineLen -= leftColOffset;
    if (i == lines.size() - 1) lineLen -= rightColOffset;
    total += max(0, lineLen);
  }
  return static_cast<double>(total);
}

bool EditSearchContext::shouldContinue() const {
  return !pq.empty() && resultsFound < totalPositions && iterations < params.maxSearchDepth;
}

optional<EditState> EditSearchContext::popNextState() {
  while (!pq.empty()) {
    EditState s = pq.top();
    pq.pop();

    // Skip if we've found a better path
    EditStateKey key = s.getKey();
    auto it = costMap.find(key);
    if (it != costMap.end() && it->second < s.getCost() - 1e-9)
      continue;

    return s;
  }
  return nullopt;
}

void EditSearchContext::exploreAllDeletions(const EditState& state,
                                            DeletionCallback onDeletion) {
  const Lines& lines = state.getLines();
  Position cursor = state.getPos();

  auto [contentStart, contentEnd, editContentLen, lastEditLine] =
      computeContentBounds(lines, cursor);

  // =========================================================================
  // Forward word deletes: de, dw, dE, dW
  // =========================================================================
  if (editContentLen > 0) {
    for (const auto& spec : Edit::FORWARD_WORD_EDITS) {
      Position endpoint = VimEndpointUtils::motionWordEndpoint(
          cursor, lines, true, spec.edgeType, spec.isBig,
          spec.skipCurrent, POSITION_OUTSIDE_BOUNDARY);

      if (endpoint == POSITION_OUTSIDE_BOUNDARY)
        continue;

      // Skip if motion didn't move - operation would be cancelled in Vim
      if (endpoint == cursor)
        continue;

      if (inBoundaryRegion(endpoint, lines))
        continue;

      if (cursor.line == lastEditLine && endpoint.line > cursor.line &&
          editBoundary.hasLinesBelow())
        continue;

      Range range(cursor, endpoint);
      onDeletion(range, spec.cmd, spec.keys);
    }
  }

  // =========================================================================
  // Backward word deletes: db, dge, dB, dgE
  // =========================================================================
  if (editContentLen > 0) {
    for (const auto& spec : Edit::BACKWARD_WORD_EDITS) {
      Position endpoint = VimEndpointUtils::motionWordEndpoint(
          cursor, lines, false, spec.edgeType, spec.isBig,
          spec.skipCurrent, POSITION_OUTSIDE_BOUNDARY);

      if (endpoint == POSITION_OUTSIDE_BOUNDARY)
        continue;

      // Skip if motion didn't move - operation would be cancelled in Vim
      if (endpoint == cursor)
        continue;

      if (inBoundaryRegion(endpoint, lines))
        continue;

      if (endpoint.line < cursor.line &&
          editBoundary.hasLinesAbove() && editBoundary.hasLinesBelow())
        continue;

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

  // =========================================================================
  // Text object deletes: diw, daw, diW, daW
  // =========================================================================
  if (editContentLen > 0) {
    for (const auto& spec : Edit::TEXT_OBJECT_EDITS) {
      Range range = VimEndpointUtils::textObjectRange(
          cursor, lines, spec.isInner, spec.isBig,
          POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);

      if (range.start == POSITION_OUTSIDE_BOUNDARY)
        continue;

      if (inBoundaryRegion(range.start, lines) || inBoundaryRegion(range.end, lines))
        continue;

      onDeletion(range, spec.cmd, spec.keys);
    }
  }

  // =========================================================================
  // Line motion deletes: D, d0
  // =========================================================================
  for (const auto& spec : Edit::HALF_LINE_EDITS) {
    if (spec.forward) {
      // D: delete from cursor to end of line
      if (cursor.line == lastEditLine && !editBoundary.atLineEnd()) continue;

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
    } else {
      // d0: delete from start of line to cursor (exclusive)
      if (cursor.line == 0 && !editBoundary.atLineStart()) continue;

      int lineContentStart = (cursor.line == 0) ? leftColOffset : 0;
      if (cursor.col <= lineContentStart) continue;
      Range range(Position(cursor.line, lineContentStart),
                  Position(cursor.line, cursor.col - 1));
      onDeletion(range, spec.cmd, spec.keys);
    }
  }

  // =========================================================================
  // Char deletes: x, X
  // NOTE: Full line operations (dd/cc) NOT handled here - each optimizer handles them
  // =========================================================================
  if (editContentLen > 0 && cursor.col >= contentStart && cursor.col < contentEnd) {
    bool isLastEditChar = (cursor.col == contentEnd - 1);
    bool wouldLandOnBoundary = isLastEditChar && rightColOffset > 0;

    if (wouldLandOnBoundary) {
      if (editContentLen == 1) {
        Range range(cursor, cursor);
        onDeletion(range, "x", Deletion::CHAR.at("x"));
      }
    } else {
      Range range(cursor, cursor);
      onDeletion(range, "x", Deletion::CHAR.at("x"));
    }
  }

  // X: delete char before cursor
  if (cursor.col > contentStart) {
    Position before(cursor.line, cursor.col - 1);
    if (!inBoundaryRegion(before, lines)) {
      Range range(before, before);
      onDeletion(range, "X", Deletion::CHAR.at("X"));
    }
  }
}
