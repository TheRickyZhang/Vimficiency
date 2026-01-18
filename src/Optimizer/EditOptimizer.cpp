// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include "Boundary/EditBoundary.h"
#include "Editor/LineRange.h"
#include "Keyboard/CharToKeys.h"
#include "Keyboard/EditToKeys.h"
#include "Keyboard/MotionToKeys.h"
#include "State/EditState.h"
#include "State/RunningEffort.h"
#include "VimCore/VimEndpointUtils.h"

#include <algorithm>
#include <optional>
#include <queue>
#include <unordered_map>

using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

namespace {

// Check if buffer is effectively empty (all lines are empty strings)
bool allLinesEmpty(const Lines &lines) {
  if (lines.empty())
    return true;
  for (const auto &line : lines) {
    if (!line.empty())
      return false;
  }
  return true;
}

// Convert delete command to change equivalent
// Returns the change command string, or empty string if no mapping exists
string deleteToChange(const string &deleteCmd) {
  assert(!deleteCmd.empty());
  if (deleteCmd == "D")
    return "C";
  if (deleteCmd == "dd")
    return "cc";
  if (deleteCmd[0] == 'd') {
    return "c" + deleteCmd.substr(1);
  }
  if (deleteCmd == "x")
    return "s";
  if (deleteCmd == "X")
    return "hs";
  assert(false && "deleteToChange not supported");
  return "";
}

// Only new lines remain
bool linesEffectivelyEmpty(const Lines &endLines) {
  if (endLines.empty())
    return true;
  if (endLines.size() == 1 && endLines[0].empty())
    return true;
  return false;
}

// Build the typed content string from endLines
pair<string, PhysicalKeys> buildTypedCommands(const Lines &endLines) {
  string str;
  PhysicalKeys keys;
  for (size_t i = 0; i < endLines.size(); i++) {
    str += endLines[i];
    for (int c : endLines[i]) {
      keys.append(CHAR_TO_KEYS.at(c));
    }
    if (i < endLines.size() - 1) {
      str += "<CR>";
      keys.push_back(Key::Key_Enter);
    }
  }
  str += "<Esc>";
  keys.push_back(Key::Key_Esc);
  return {str, keys};
}

} // anonymous namespace

// =============================================================================
// Heuristic for A* search
// =============================================================================

double EditOptimizer::heuristic(const Lines &lines) const {
  double total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    total += lines[i].size();
    if (i < lines.size() - 1)
      total += 1;
  }
  return total;
}

// =============================================================================
// tryReplacement - replacement strategy for same-length transformations
// =============================================================================

void tryReplacement(const string &deleted, const string &inserted,
                    const Config &config, int &lastReplacementPos,
                    vector<Result> &res) {
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
    while (j + 1 < diff.size() && diff[j + 1] == diff[j] + 1 &&
           inserted[diff[j + 1]] == inserted[diff[j]]) {
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
    i = j + 1;
    if (i < diff.size()) {
      int prevPos = diff[j];
      int nextPos = diff[i];
      int dist = nextPos - prevPos;

      if (dist == 1) {
        seq += "l";
      } else if (dist > 2) {
        // Try f-motion: check if target char appears only once in range
        char findChar = deleted[nextPos]; // char at target position in original
        int occurrences = (count(deleted.begin() + prevPos + 1,
                                 deleted.begin() + nextPos, findChar));
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

EditResult
EditOptimizer::optimizeEdit(const Lines &startLines, const Lines &endLines,
                            EditBoundary editBoundary,
                            const optional<OptimizerParams> &paramsOverride) {
  const OptimizerParams &params = paramsOverride.value_or(defaultParams);

  assert(startLines != endLines);

  int totalPositions = 0;
  for (const auto &line : startLines) {
    totalPositions += line.empty() ? 1 : static_cast<int>(line.size());
  }

  vector<Result> replacementResults;
  int lastReplacementPos = -1;
  if (startLines.size() == 1 && endLines.size() == 1 &&
      startLines[0].size() == endLines[0].size() && !startLines[0].empty()) {
    tryReplacement(startLines[0], endLines[0], config, lastReplacementPos,
                   replacementResults);
  }
  EditResult result(totalPositions, static_cast<int>(replacementResults.size()),
                    lastReplacementPos);
  // Copy replacement results into the EditResult
  for (size_t i = 0; i < replacementResults.size(); i++) {
    result.replacementResults[i] = replacementResults[i];
  }

  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;
  unordered_map<EditStateKey, double, EditStateKeyHash> costMap;

  // Initialize with all starting positions
  int startIndex = 0;
  double startCost = heuristic(startLines);
  for (int line = 0; line < static_cast<int>(startLines.size()); line++) {
    int lineCols = startLines[line].empty()
                       ? 1
                       : static_cast<int>(startLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      pq.push(
          EditState(startLines, Position(line, col), startIndex, startCost));
      startIndex++;
    }
  }

  // Precompute typed content for goal state (content to type after change
  // operation)
  auto [typedStr, typedKeys] = buildTypedCommands(endLines);

  int resultsFound = 0;
  int iterations = 0;

  auto exploreNewState = [&](EditState &&newState) {
    EditStateKey key = newState.getKey();
    auto it = costMap.find(key);

    double newCost = newState.getCost();
    if (it == costMap.end() || newCost < it->second) {
      costMap[key] = newCost;
      pq.push(std::move(newState));
    }
  };

  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const char *deleteCmd,
                             const PhysicalKeys &deleteKeys) {
    EditState newState = base;
    newState.applyDeletion(range);

    // Goal reached - all lines are empty
    if (allLinesEmpty(newState.getLines())) {
      int idx = newState.getStartIndex();

      // Skip if we already have a result for this starting position
      // (A* guarantees first result is optimal)
      if (result.typeAllResults[idx].isValid()) {
        return;
      }

      // Use change equivalent instead of delete
      string changeCmd = deleteToChange(deleteCmd);

      // Build sequence to collapse multiple empty lines to one
      const Lines &lines = newState.getLines();
      int lineIndex = newState.getPos().line;
      int lineCount = static_cast<int>(lines.size());

      string collapseSeq;
      PhysicalKeys collapseKeys;

      // Backspace to get to first line (each BS merges with previous line)
      for (int i = 0; i < lineIndex; i++) {
        collapseSeq += "<BS>";
        collapseKeys.push_back(Key::Key_Backspace);
      }

      // Delete to remove remaining lines below
      for (int i = 0; i < lineCount - lineIndex - 1; i++) {
        collapseSeq += "<Del>";
        collapseKeys.push_back(Key::Key_Delete);
      }

      string seqStr = newState.getSeq() + changeCmd + collapseSeq + typedStr;

      // Compute effort incrementally
      PhysicalKeys changeKeys = globalTokenizer().tokenize(changeCmd);
      RunningEffort effort = newState.getRunningEffort();
      effort.append(changeKeys, config);
      effort.append(collapseKeys, config);
      double totalEffort = effort.append(typedKeys, config);

      result.typeAllResults[idx] = Result(seqStr, totalEffort);
      resultsFound++;
      return;
    }

    // Not goal: append delete cmd, continue search
    newState.appendToSeq(deleteCmd);
    newState.updateEffort(deleteKeys, config);
    newState.updateCost(newState.getEffort() + heuristic(newState.getLines()));
    exploreNewState(std::move(newState));
  };

  while (!pq.empty() && resultsFound < totalPositions &&
         iterations < params.maxSearchDepth) {
    iterations++;

    EditState s = pq.top();
    pq.pop();

    // Skip if we've found a better path
    EditStateKey key = s.getKey();
    auto it = costMap.find(key);
    if (it != costMap.end() && it->second < s.getCost() - 1e-9)
      continue;

    const Lines &lines = s.getLines();
    Position cursor = s.getPos();
    int lineLen = static_cast<int>(lines[cursor.line].size());

    // =========================================================================
    // Forward word deletes: de, dw, dE, dW
    // Skip if on empty line - change equivalents (ce, cw) can't start there
    // =========================================================================
    if (lineLen > 0) {
      for (const auto &spec : Edit::FORWARD_WORD_EDITS) {
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
            cursor, lines, true, spec.edgeType, spec.isBig, spec.skipCurrent,
            POSITION_OUTSIDE_BOUNDARY);

        if (endpoint == POSITION_OUTSIDE_BOUNDARY)
          continue;

        Range range(cursor, endpoint);
        exploreDeletion(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Backward word deletes: db, dge, dB, dgE
    // Skip if on empty line - change equivalents can't start there
    // =========================================================================
    if (lineLen > 0) {
      for (const auto &spec : Edit::BACKWARD_WORD_EDITS) {
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
            cursor, lines, false, spec.edgeType, spec.isBig, spec.skipCurrent,
            POSITION_OUTSIDE_BOUNDARY);

        if (endpoint == POSITION_OUTSIDE_BOUNDARY)
          continue;

        // Build range respecting cursor exclusivity
        Range range;
        if (spec.isExclusiveAtCursor && cursor.col > 0) {
          range = Range(endpoint, Position(cursor.line, cursor.col - 1));
        } else {
          range = Range(endpoint, cursor);
        }

        exploreDeletion(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Text object deletes: diw, daw, diW, daW
    // Skip if on empty line - change equivalents can't start there
    // =========================================================================
    if (lineLen > 0) {
      for (const auto &spec : Edit::TEXT_OBJECT_EDITS) {
        Range range = VimEndpointUtils::textObjectRange(
            cursor, lines, spec.isInner, spec.isBig, POSITION_OUTSIDE_BOUNDARY,
            POSITION_OUTSIDE_BOUNDARY);

        if (range.start == POSITION_OUTSIDE_BOUNDARY)
          continue;

        exploreDeletion(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Line motion deletes: D, d0
    // =========================================================================
    for (const auto &spec : Edit::LINE_EDITS) {
      int endCol = VimEndpointUtils::motionLineEndpoint(
          cursor, lines, spec.forward, editBoundary);
      if (endCol == VimEndpointUtils::COL_OUTSIDE_BOUNDARY)
        continue;

      Range range;
      if (spec.forward) {
        // D: cursor to end of line
        if (lineLen == 0)
          continue; // nothing to delete on empty line
        range = Range(cursor, Position(cursor.line, endCol));
      } else {
        // d0: start of line to cursor (exclusive)
        if (cursor.col == 0)
          continue; // nothing to delete at col 0
        range = Range(Position(cursor.line, 0),
                      Position(cursor.line, cursor.col - 1));
      }
      exploreDeletion(s, range, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Full line deletes: dd
    // =========================================================================
    for (const auto &spec : Edit::FULL_LINE_EDITS) {
      LineRange lineRange = VimEndpointUtils::lineDeleteRange(cursor, lines, editBoundary);
      if (!lineRange.isValid())
        continue;

      int endCol = lineLen > 0 ? lineLen - 1 : 0;
      Range range(Position(cursor.line, 0), Position(cursor.line, endCol));
      exploreDeletion(s, range, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Char deletes: x, X
    // =========================================================================
    if (lineLen > 0 && cursor.col < lineLen) {
      Range range(cursor, cursor);
      exploreDeletion(s, range, "x", Deletion::CHAR.at("x"));
    }

    if (cursor.col > 0) {
      Position before(cursor.line, cursor.col - 1);
      Range range(before, before);
      exploreDeletion(s, range, "X", Deletion::CHAR.at("X"));
    }
  }

  return result;
}
