// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include "Boundary/EditBoundary.h"
#include "Editor/LineRange.h"
#include "Keyboard/EditToKeys.h"
#include "Keyboard/MotionToKeys.h"
#include "State/EditState.h"
#include "State/RunningEffort.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <optional>

using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

namespace {

// Check if buffer is effectively empty (goal state)
bool isBufferEmpty(const Lines& lines) {
  if (lines.empty()) return true;
  if (lines.size() == 1 && lines[0].empty()) return true;
  return false;
}

// =============================================================================
// Helper: Try to add a new state to the search
// =============================================================================

void tryAddState(
    EditState& newState,
    priority_queue<EditState, vector<EditState>, greater<EditState>>& pq,
    unordered_map<EditStateKey, double, EditStateKeyHash>& costMap) {

  EditStateKey key = newState.getKey();
  auto it = costMap.find(key);

  if (it == costMap.end() || newState.cost < it->second) {
    costMap[key] = newState.cost;
    pq.push(newState);
  }
}

}  // anonymous namespace


// =============================================================================
// Heuristic for A* search
// =============================================================================

double EditOptimizer::heuristic(const Lines& lines) const {
  double total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    total += lines[i].size();
    if (i < lines.size() - 1) total += 1;
  }
  return total;
}

// =============================================================================
// tryReplacement - replacement strategy for same-length transformations
// =============================================================================

void tryReplacement(const string& deleted, const string& inserted, const Config& config,
                    int& lastReplacementPos, vector<Result>& res) {
  assert(deleted.size() == inserted.size());
  assert(deleted != inserted);

  // Find all differing positions
  vector<int> diff;
  for (size_t i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) {
      diff.push_back(static_cast<int>(i));
    }
  }

  int sz = static_cast<int>(diff.size());
  int firstDiff = diff[0];
  lastReplacementPos = diff.back();

  // stats[i] = (prefix to move from position i to firstDiff, effort)
  vector<pair<string, RunningEffort>> stats(firstDiff + 1);
  for (int i = 0; i <= firstDiff; i++) {
    pair<string, RunningEffort> temp;
    if (i < firstDiff) {
      temp.first = string(firstDiff - i, 'l');
      temp.second.append(PhysicalKeys(firstDiff - i, Key::Key_L), config);
    }
    stats[i] = temp;
  }

  // Build replacement sequence from firstDiff onward
  // Group consecutive diff positions into runs, use R-mode for runs of 2+
  string seq;
  size_t i = 0;
  while (i < diff.size()) {
    int runStart = diff[i];

    // Find consecutive positions (for R-mode)
    size_t j = i;
    while (j + 1 < diff.size() && diff[j+1] == diff[j] + 1 && inserted[diff[j+1]] == inserted[diff[j]]) {
      j++;
    }

    int runLength = static_cast<int>(j - i + 1);
    if (runLength == 1) {
      seq += "r";
      seq += inserted[runStart];
    } else {
      // Can use {cnt}r if same consecutive inserted
      seq += to_string(runLength) + "r" + inserted[runStart];
    }

    // Navigate to next run if there is one
    i = j+1;
    if (i < diff.size()) {
      int prevPos = diff[j];
      int nextPos = diff[i];
      int dist = nextPos - prevPos;

      if (dist == 1) {
        seq += "l";
      } else if (dist > 2) {
        // Try f-motion: check if target char appears only once in range
        char findChar = deleted[nextPos];  // char at target position in original
        int occurrences = ( count(deleted.begin() + prevPos + 1, deleted.begin() + nextPos, findChar));
        if (occurrences == 0) {
          seq += "f";
          seq += findChar;
        } else {
          seq += to_string(dist) + "l";
        }
      } else {
        seq += to_string(dist) + "l";
      }
    }
  }

  // Build results for each starting position
  res.resize(stats.size());
  for (size_t k = 0; k < stats.size(); k++) {
    auto [prefix, runningEffort] = stats[k];
    PhysicalKeys keys = globalTokenizer().tokenize(seq);
    double effort = runningEffort.append(keys, config);
    res[k] = Result(prefix + seq, effort);
  }
}

// =============================================================================
// optimizeEdit - main entry point
// =============================================================================

EditResult EditOptimizer::optimizeEdit(const Lines& startLines, const Lines& endLines,
                                        EditBoundary editBoundary,
                                        const optional<OptimizerParams>& paramsOverride) {
  const OptimizerParams& params = paramsOverride.value_or(defaultParams);
  
  assert(startLines != endLines);

  int totalPositions = 0;
  for (const auto& line : startLines) {
    totalPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }

  vector<Result> replacementResults;
  int lastReplacementPos = -1;
  if (startLines.size() == 1 && endLines.size() == 1 &&
      startLines[0].size() == endLines[0].size() &&
      !startLines[0].empty()) {
    tryReplacement(startLines[0], endLines[0], config,
                   lastReplacementPos, replacementResults);
  }
  EditResult result(totalPositions,
                    static_cast<int>(replacementResults.size()),
                    lastReplacementPos);

  // Copy replacement results into the EditResult
  for (size_t i = 0; i < replacementResults.size(); i++) {
    result.replacementResults[i] = replacementResults[i];
  }

  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;
  unordered_map<EditStateKey, double, EditStateKeyHash> costMap;

  // Initialize with all starting positions
  int startIndex = 0;
  int startCost = heuristic(startLines);
  for (int line = 0; line < static_cast<int>(startLines.size()); line++) {
    int lineCols = startLines[line].empty() ? 1 : static_cast<int>(startLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      pq.push(EditState(startLines, Position(line, col), startIndex, startCost));
      startIndex++;
    }
  }

  int resultsFound = 0;
  int iterations = 0;

  // Helper lambda to apply deletion and add state
  auto applyAndAdd = [&](const EditState& s, const Range& range,
                         const char* cmd, const PhysicalKeys& keys) {
    EditState newState = s;
    VimEditUtils::deleteRange(newState.lines, range, newState.pos, Mode::Normal);
    newState.effort.append(keys, config);
    newState.seq.push_back(cmd);
    newState.cost = newState.effort.getEffort(config) + heuristic(newState.lines);
    tryAddState(newState, pq, costMap);
  };

  while (!pq.empty()
    && resultsFound < totalPositions
    && iterations < params.maxSearchDepth
  ) {
    iterations++;

    EditState s = pq.top();
    pq.pop();

    // Goal check
    if (isBufferEmpty(s.lines)) {
      // TODO: set most recent delete to change, do backspace/delete until one empty line, then type everything out
      int idx = s.startIndex;
      if (!result.typeAllResults[idx].isValid()) {
        string seqStr;
        for (const auto& op : s.seq) {
          seqStr += op;
        }
        result.typeAllResults[idx] = Result(seqStr, s.effort.getEffort(config));
        resultsFound++;
      }
      continue;
    }

    // Skip if we've found a better path
    EditStateKey key = s.getKey();
    auto it = costMap.find(key);
    if (it != costMap.end() && it->second < s.cost - 1e-9) continue;

    const Lines& lines = s.lines;
    Position cursor = s.pos;
    int lineLen = lines[cursor.line].size();

    // =========================================================================
    // Forward word deletes: de, dw, dE, dW
    // =========================================================================
    for (const auto& spec : Edit::FORWARD_WORD_EDITS) {
      Position endpoint = VimEndpointUtils::motionWordEndpoint(
          cursor, lines, true, spec.edgeType, spec.isBig,
          spec.skipCurrent, POSITION_OUTSIDE_BOUNDARY);

      if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

      Range range(cursor, endpoint);
      applyAndAdd(s, range, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Backward word deletes: db, dge, dB, dgE
    // =========================================================================
    for (const auto& spec : Edit::BACKWARD_WORD_EDITS) {
      Position endpoint = VimEndpointUtils::motionWordEndpoint(
          cursor, lines, false, spec.edgeType, spec.isBig,
          spec.skipCurrent, POSITION_OUTSIDE_BOUNDARY);

      if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

      // Build range respecting cursor exclusivity
      Range range;
      if (spec.isExclusiveAtCursor && cursor.col > 0) {
        // db/dB: don't include cursor char
        range = Range(endpoint, Position(cursor.line, cursor.col - 1));
      } else if (spec.isExclusiveAtCursor) {
        // At col 0, nothing to delete for db/dB from cursor
        continue;
      } else {
        range = Range(endpoint, cursor);
      }

      applyAndAdd(s, range, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Text object deletes: diw, daw, diW, daW
    // =========================================================================
    for (const auto& spec : Edit::TEXT_OBJECT_EDITS) {
      Range range = VimEndpointUtils::textObjectRange(
          cursor, lines, spec.isInner, spec.isBig,
          POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);

      if (range.start == POSITION_OUTSIDE_BOUNDARY) continue;

      applyAndAdd(s, range, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Line motion deletes: D, d0
    // =========================================================================
    for (const auto& spec : Edit::LINE_EDITS) {
      int endCol = VimEndpointUtils::motionLineEndpoint(cursor, lines, spec.forward, editBoundary);
      if (endCol == VimEndpointUtils::COL_OUTSIDE_BOUNDARY) continue;

      Range range;
      if (spec.forward) {
        // D: cursor to end of line
        if (lineLen == 0) continue;  // nothing to delete on empty line
        range = Range(cursor, Position(cursor.line, endCol));
      } else {
        // d0: start of line to cursor (exclusive)
        if (cursor.col == 0) continue;  // nothing to delete at col 0
        range = Range(Position(cursor.line, 0), Position(cursor.line, cursor.col - 1));
      }
      applyAndAdd(s, range, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Full line deletes: dd
    // =========================================================================
    for (const auto& spec : Edit::FULL_LINE_EDITS) {
      LineRange lineRange = VimEndpointUtils::lineDeleteRange(cursor, lines, editBoundary);
      if (!lineRange.isValid()) continue;

      int endCol = lineLen > 0 ? lineLen - 1 : 0;
      Range range(Position(cursor.line, 0), Position(cursor.line, endCol));
      applyAndAdd(s, range, spec.cmd, spec.keys);
    }

    // Char deletes: x, X
    if (lineLen > 0 && cursor.col < lineLen) {
      Range range(cursor, cursor);
      applyAndAdd(s, range, "x", Deletion::CHAR.at("x"));
    }

    if (cursor.col > 0) {
      Position before(cursor.line, cursor.col - 1);
      Range range(before, before);
      applyAndAdd(s, range, "X", Deletion::CHAR.at("X"));
    }
  }

  return result;
}
