// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include "Boundary/EditBoundary.h"
#include "Keyboard/CharToKeys.h"
#include "Keyboard/EditToKeys.h"
#include "State/EditState.h"
#include "State/RunningEffort.h"
#include "VimCore/VimEditUtils.h"
#include "VimCore/VimEndpointUtils.h"

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

double EditOptimizer::heuristic(const EditState& s, const OptimizerParams& params) const {
  double total = 0;
  for (size_t i = 0; i < s.lines.size(); i++) {
    total += s.lines[i].size();
    if (i < s.lines.size() - 1) total += 1;
  }
  return total;
}

// =============================================================================
// tryReplacement - replacement strategy for same-length transformations
// =============================================================================

Result tryReplacement(const string& deleted, const string& inserted, const Config& config) {
  if (deleted.size() != inserted.size()) return Result();
  if (deleted.find('\n') != string::npos || inserted.find('\n') != string::npos) return Result();
  if (deleted.empty()) return Result();

  vector<int> diffPositions;
  for (size_t i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) {
      diffPositions.push_back(static_cast<int>(i));
    }
  }

  if (diffPositions.empty()) return Result();

  string seq;
  RunningEffort effort;
  int currentCol = 0;

  size_t i = 0;
  while (i < diffPositions.size()) {
    int pos = diffPositions[i];

    int dist = pos - currentCol;
    if (dist > 0) {
      if (dist == 1) {
        seq += "l";
        effort.append({Key::Key_L}, config);
      } else {
        seq += to_string(dist) + "l";
        string distStr = to_string(dist);
        for (char c : distStr) {
          auto it = CHAR_TO_KEYS.find(c);
          if (it != CHAR_TO_KEYS.end()) {
            effort.append(it->second, config);
          }
        }
        effort.append({Key::Key_L}, config);
      }
      currentCol = pos;
    }

    int runLength = 1;
    while (i + runLength < diffPositions.size() &&
           diffPositions[i + runLength] == pos + runLength) {
      runLength++;
    }

    if (runLength == 1) {
      char newChar = inserted[pos];
      seq += "r";
      seq += newChar;
      effort.append({Key::Key_R}, config);
      auto it = CHAR_TO_KEYS.find(newChar);
      if (it != CHAR_TO_KEYS.end()) {
        effort.append(it->second, config);
      }
      currentCol = pos;
    } else {
      seq += "R";
      effort.append({Key::Key_Shift, Key::Key_R}, config);
      for (int j = 0; j < runLength; j++) {
        char newChar = inserted[pos + j];
        seq += newChar;
        auto it = CHAR_TO_KEYS.find(newChar);
        if (it != CHAR_TO_KEYS.end()) {
          effort.append(it->second, config);
        }
      }
      seq += "<Esc>";
      effort.append({Key::Key_Esc}, config);
      currentCol = pos + runLength - 1;
    }

    i += runLength;
  }

  return Result(seq, effort.getEffort(config));
}

// =============================================================================
// optimizeEdit - main entry point
// =============================================================================

EditResult EditOptimizer::optimizeEdit(const Lines& startLines, const Lines& endLines,
                                        EditBoundary editBoundary,
                                        const optional<OptimizerParams>& paramsOverride) {
  const OptimizerParams& params = paramsOverride.value_or(defaultParams);

  if (startLines == endLines) {
    EditResult result(1, 0, -1);
    result.typeAllResults[0] = Result("", 0.0);
    return result;
  }

  if (!isBufferEmpty(endLines)) {
    return EditResult(0, 0, -1);
  }

  int totalPositions = 0;
  for (const auto& line : startLines) {
    totalPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }

  EditResult result(totalPositions, 0, -1);

  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;
  unordered_map<EditStateKey, double, EditStateKeyHash> costMap;

  // Initialize with all starting positions
  int startIndex = 0;
  for (int line = 0; line < static_cast<int>(startLines.size()); line++) {
    int lineCols = startLines[line].empty() ? 1 : static_cast<int>(startLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      EditState initial;
      initial.lines = startLines;
      initial.pos = Position(line, col);
      initial.mode = Mode::Normal;
      initial.startIndex = startIndex;
      initial.cost = heuristic(initial, params);
      pq.push(initial);
      startIndex++;
    }
  }

  int resultsFound = 0;
  int maxIterations = params.maxSearchDepth;
  int iterations = 0;

  // Helper lambda to apply deletion and add state
  auto applyAndAdd = [&](const EditState& s, const Range& range,
                         const char* cmd, const PhysicalKeys& keys) {
    EditState newState = s;
    VimEditUtils::deleteRange(newState.lines, range, newState.pos, Mode::Normal);
    newState.effort.append(keys, config);
    newState.seq.push_back(cmd);
    newState.cost = newState.effort.getEffort(config) + heuristic(newState, params);
    tryAddState(newState, pq, costMap);
  };

  while (!pq.empty() && resultsFound < totalPositions && iterations < maxIterations) {
    iterations++;

    EditState s = pq.top();
    pq.pop();

    // Goal check
    if (isBufferEmpty(s.lines)) {
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
    int lineLen = lines[cursor.line].empty() ? 0 : static_cast<int>(lines[cursor.line].size());

    // =========================================================================
    // Forward word deletes: de, dw, dE, dW
    // Skip on empty lines - word motions require content at cursor
    // =========================================================================
    if (lineLen > 0) {
      for (const auto& spec : Edit::FORWARD_WORD_EDITS) {
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
            cursor, lines, true, spec.edgeType, spec.isBig,
            false, POSITION_OUTSIDE_BOUNDARY);

        if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

        Range range(cursor, endpoint);
        applyAndAdd(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Backward word deletes: db, dge, dB, dgE
    // Skip on empty lines - word motions require content at cursor
    // =========================================================================
    if (lineLen > 0) {
      for (const auto& spec : Edit::BACKWARD_WORD_EDITS) {
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
            cursor, lines, false, spec.edgeType, spec.isBig,
            false, POSITION_OUTSIDE_BOUNDARY);

        if (endpoint == POSITION_OUTSIDE_BOUNDARY) continue;

        Range range(endpoint, cursor);
        applyAndAdd(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Text object deletes: diw, daw, diW, daW
    // Skip on empty lines - text objects require content
    // =========================================================================
    if (lineLen > 0) {
      for (const auto& spec : Edit::TEXT_OBJECT_EDITS) {
        Range range = VimEndpointUtils::textObjectRange(
            cursor, lines, spec.isInner, spec.isBig,
            POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);

        if (range.start == POSITION_OUTSIDE_BOUNDARY) continue;

        applyAndAdd(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Line deletes: dd, D
    // =========================================================================

    // dd - delete entire line (only if full line edit is safe)
    if (editBoundary.isFullLineEditSafe()) {
      Range range(Position(cursor.line, 0),
                  Position(cursor.line, max(0, lineLen - 1)));
      applyAndAdd(s, range, "dd", Deletion::LINE.at("dd"));
    }

    // D - delete from cursor to end of line (only if at line end boundary)
    if (editBoundary.atLineEnd() && lineLen > 0) {
      Range range(cursor, Position(cursor.line, lineLen - 1));
      applyAndAdd(s, range, "D", Deletion::LINE.at("D"));
    }

    // =========================================================================
    // Char deletes: x, X
    // =========================================================================

    // x - delete char at cursor
    if (lineLen > 0 && cursor.col < lineLen) {
      Range range(cursor, cursor);
      applyAndAdd(s, range, "x", Deletion::CHAR.at("x"));
    }

    // X - delete char before cursor
    if (cursor.col > 0) {
      Position before(cursor.line, cursor.col - 1);
      Range range(before, before);
      applyAndAdd(s, range, "X", Deletion::CHAR.at("X"));
    }
  }

  return result;
}
