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

  // ===========================================================================
  // Build effectiveLines - edit region with prefix/suffix on boundary lines
  // ===========================================================================
  // No synthetic empty lines. Just the edit region content with:
  // - prefix prepended to first line (if any)
  // - suffix appended to last line (if any)
  Lines effectiveLines;
  int leftColOffset = 0;   // Chars before edit content on first line (prefix length)
  int rightColOffset = 0;  // Chars after edit content on last line (suffix length)

  for (size_t i = 0; i < startLines.size(); i++) {
    string line = startLines[i];

    if (i == 0 && !editBoundary.prefix.empty()) {
      line = editBoundary.prefix + line;
      leftColOffset = static_cast<int>(editBoundary.prefix.size());
    }

    if (i == startLines.size() - 1 && !editBoundary.suffix.empty()) {
      line += editBoundary.suffix;
      rightColOffset = static_cast<int>(editBoundary.suffix.size());
    }

    effectiveLines.push_back(line);
  }

  // ===========================================================================
  // Goal check: all edit content deleted, only prefix/suffix/empty lines remain
  // ===========================================================================
  // Returns true if the lines represent a valid goal state where:
  // - All edit content is deleted
  // - Prefix (if any) is preserved on first line
  // - Suffix (if any) is preserved on last line
  // - Any number of empty lines in between (will be collapsed with <BS>/<Del>)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.empty()) {
      // Empty buffer is goal only if no prefix/suffix to preserve
      return editBoundary.prefix.empty() && editBoundary.suffix.empty();
    }

    // Check each line contains only boundary content or is empty
    for (size_t i = 0; i < lines.size(); i++) {
      const string &line = lines[i];

      if (i == 0 && !editBoundary.prefix.empty()) {
        // First line must start with prefix, rest must be empty or suffix
        if (line.size() < editBoundary.prefix.size()) return false;
        if (line.substr(0, editBoundary.prefix.size()) != editBoundary.prefix) return false;

        // Content after prefix
        string afterPrefix = line.substr(editBoundary.prefix.size());
        if (lines.size() == 1 && !editBoundary.suffix.empty()) {
          // Single line: must end with suffix, nothing between
          if (afterPrefix != editBoundary.suffix) return false;
        } else {
          // Multi-line or no suffix: nothing after prefix
          if (!afterPrefix.empty()) return false;
        }
      } else if (i == lines.size() - 1 && !editBoundary.suffix.empty()) {
        // Last line must be just suffix (or empty if suffix will be added)
        if (line != editBoundary.suffix) return false;
      } else {
        // Middle lines must be empty
        if (!line.empty()) return false;
      }
    }
    return true;
  };

  // ===========================================================================
  // Boundary protection: positions that must not be touched
  // ===========================================================================
  auto inBoundaryRegion = [&](const Position &pos, const Lines &lines) {
    if (lines.empty()) return true;

    int lastLine = static_cast<int>(lines.size()) - 1;

    // Line boundary checks (can't go above first or below last line)
    if (pos.line < 0 || pos.line > lastLine) return true;

    // Left column boundary (prefix on first line)
    if (pos.line == 0 && pos.col < leftColOffset) return true;

    // Right column boundary (suffix on last line)
    if (pos.line == lastLine && rightColOffset > 0) {
      int lineLen = static_cast<int>(lines[pos.line].size());
      if (pos.col >= lineLen - rightColOffset) return true;
    }

    return false;
  };

  // For tracking last edit line (affected by hasLinesBelow for dd)
  int lastEditLine = static_cast<int>(effectiveLines.size()) - 1;

  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;
  unordered_map<EditStateKey, double, EditStateKeyHash> costMap;

  // Initialize with all starting positions (in effectiveLines coordinates)
  int startIndex = 0;
  double startCost = heuristic(startLines);  // Heuristic based on content to delete
  for (int line = 0; line < static_cast<int>(startLines.size()); line++) {
    int lineCols = startLines[line].empty()
                       ? 1
                       : static_cast<int>(startLines[line].size());
    for (int col = 0; col < lineCols; col++) {
      // Convert to effectiveLines coordinates (add prefix offset on first line)
      int effCol = col + (line == 0 ? leftColOffset : 0);
      pq.push(
          EditState(effectiveLines, Position(line, effCol), startIndex, startCost));
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

    const Lines &lines = newState.getLines();

    // Goal reached - all edit content deleted, only boundary content remains
    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.typeAllResults[idx].isValid()) return;

      string changeCmd = deleteToChange(deleteCmd);
      int cursorLine = newState.getPos().line;

      // Collapse multiple lines into one with <BS>/<Del> after entering insert mode.
      // To merge N lines into 1, we need N-1 join operations.
      // <BS> joins current line with previous, <Del> joins with next.
      string collapseSeq;
      PhysicalKeys collapseKeys;

      int totalLines = static_cast<int>(lines.size());
      if (totalLines > 1) {
        // Lines before cursor: each needs <BS> to join upward
        int linesBefore = cursorLine;
        // Lines after cursor: each needs <Del> to join downward
        int linesAfter = totalLines - 1 - cursorLine;

        for (int i = 0; i < linesBefore; i++) {
          collapseSeq += "<BS>";
          collapseKeys.push_back(Key::Key_Backspace);
        }
        for (int i = 0; i < linesAfter; i++) {
          collapseSeq += "<Del>";
          collapseKeys.push_back(Key::Key_Delete);
        }
      }

      string seqStr = newState.getSeq() + changeCmd + collapseSeq + typedStr;

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

    // Heuristic: count remaining edit content (excluding prefix/suffix)
    double remaining = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
      int start = (i == 0) ? leftColOffset : 0;
      int end = static_cast<int>(lines[i].size());
      if (i == static_cast<int>(lines.size()) - 1 && rightColOffset > 0) {
        end -= rightColOffset;
      }
      remaining += max(0, end - start);
      if (i < static_cast<int>(lines.size()) - 1) {
        remaining += 1;  // Newline
      }
    }
    newState.updateCost(newState.getEffort() + remaining);
    exploreNewState(std::move(newState));
  };

  // Linewise deletion for dd - deletes entire line including newline.
  // With hasLinesBelow, cursor may need "k" to stay in edit region.
  auto exploreLinewiseDeletion = [&](const EditState &base, int line,
                                     const char *deleteCmd,
                                     const PhysicalKeys &deleteKeys) {
    EditState newState = base;
    newState.applyLinewiseDeletion(line);

    const Lines &lines = newState.getLines();

    // Build command sequence: dd + optional k to adjust cursor
    string cmdSeq = deleteCmd;
    PhysicalKeys cmdKeys = deleteKeys;

    // After linewise deletion, cursor may land beyond our edit region
    // (e.g., on content that's outside in the real buffer).
    // Add "k" to move back if needed.
    Position pos = newState.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    if (editBoundary.hasLinesBelow && lastValidLine >= 0 && pos.line > lastValidLine) {
      // Cursor escaped below - move back up
      cmdSeq += "k";
      cmdKeys.push_back(Key::Key_K);
      pos.line = lastValidLine;
      pos.col = lines[pos.line].empty() ? 0 :
                min(pos.col, static_cast<int>(lines[pos.line].size()) - 1);
      newState.setPos(pos);
    }

    // Check if goal reached
    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.typeAllResults[idx].isValid()) return;

      // Build collapse sequence for multi-line goal states
      string collapseSeq;
      PhysicalKeys collapseKeys;

      int totalLines = static_cast<int>(lines.size());
      int cursorLine = newState.getPos().line;
      if (totalLines > 1) {
        int linesBefore = cursorLine;
        int linesAfter = totalLines - 1 - cursorLine;

        for (int i = 0; i < linesBefore; i++) {
          collapseSeq += "<BS>";
          collapseKeys.push_back(Key::Key_Backspace);
        }
        for (int i = 0; i < linesAfter; i++) {
          collapseSeq += "<Del>";
          collapseKeys.push_back(Key::Key_Delete);
        }
      }

      // For pure deletion (no typed content), need to enter insert mode to collapse
      // For typing content, change command enters insert mode
      string seqStr;
      double totalEffort;
      RunningEffort effort = newState.getRunningEffort();
      effort.append(cmdKeys, config);

      if (collapseSeq.empty() && typedStr == "<Esc>") {
        // Pure deletion with no collapse needed: just use dd commands
        seqStr = newState.getSeq() + cmdSeq;
        totalEffort = effort.getEffort(config);
      } else {
        // Need to type content or collapse: add change command
        string changeCmd = deleteToChange(deleteCmd);
        seqStr = newState.getSeq() + cmdSeq + changeCmd + collapseSeq + typedStr;
        PhysicalKeys changeKeys = globalTokenizer().tokenize(changeCmd);
        effort.append(changeKeys, config);
        effort.append(collapseKeys, config);
        totalEffort = effort.append(typedKeys, config);
      }

      result.typeAllResults[idx] = Result(seqStr, totalEffort);
      resultsFound++;
      return;
    }

    // Not goal: continue search
    newState.appendToSeq(cmdSeq.c_str());
    newState.updateEffort(cmdKeys, config);

    // Heuristic: count remaining edit content (excluding prefix/suffix)
    double remaining = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
      int start = (i == 0) ? leftColOffset : 0;
      int end = static_cast<int>(lines[i].size());
      if (i == static_cast<int>(lines.size()) - 1 && rightColOffset > 0) {
        end -= rightColOffset;
      }
      remaining += max(0, end - start);
      if (i < static_cast<int>(lines.size()) - 1) remaining += 1;
    }
    newState.updateCost(newState.getEffort() + remaining);
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

    // Skip empty buffers - can happen after linewise deletions
    if (lines.empty()) continue;

    // Compute line length for edit region content (excluding boundary chars)
    int rawLineLen = static_cast<int>(lines[cursor.line].size());
    int contentStart = (cursor.line == 0) ? leftColOffset : 0;
    int contentEnd = rawLineLen;
    // Last edit line is simply the last line in effectiveLines
    int lastEditLine = static_cast<int>(lines.size()) - 1;
    // Only adjust for suffix on last line if there's suffix content
    if (cursor.line == lastEditLine && rightColOffset > 0) {
      contentEnd -= rightColOffset;
    }
    int editContentLen = max(0, contentEnd - contentStart);

    // =========================================================================
    // Forward word deletes: de, dw, dE, dW
    // Skip if on empty line - change equivalents (ce, cw) can't start there
    // =========================================================================
    if (editContentLen > 0) {
      for (const auto &spec : Edit::FORWARD_WORD_EDITS) {
        // Motions operate directly on effectiveLines (which is `lines`)
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
            cursor, lines, true, spec.edgeType, spec.isBig,
            spec.skipCurrent, POSITION_OUTSIDE_BOUNDARY);

        if (endpoint == POSITION_OUTSIDE_BOUNDARY)
          continue;

        // If skipCurrent motion returns cursor (couldn't move), skip it.
        // This happens when at word boundary with no next word to go to.
        if (spec.skipCurrent && endpoint == cursor)
          continue;

        // Check if motion endpoint is in protected boundary region
        if (inBoundaryRegion(endpoint, lines))
          continue;

        // When fully embedded (hasLinesAbove AND hasLinesBelow), block all forward
        // line-crossing to prevent escape after merge operations diverge.
        // When only hasLinesBelow, block from last line since that's where escape happens.
        if (endpoint.line > cursor.line &&
            (editBoundary.hasLinesAbove && editBoundary.hasLinesBelow))
          continue;
        if (cursor.line == lastEditLine && endpoint.line > cursor.line &&
            editBoundary.hasLinesBelow && !editBoundary.hasLinesAbove)
          continue;

        // Range is already in effectiveLines coordinates
        Range range(cursor, endpoint);
        exploreDeletion(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Backward word deletes: db, dge, dB, dgE
    // Skip if on empty line - change equivalents can't start there
    // =========================================================================
    if (editContentLen > 0) {
      for (const auto &spec : Edit::BACKWARD_WORD_EDITS) {
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
            cursor, lines, false, spec.edgeType, spec.isBig,
            spec.skipCurrent, POSITION_OUTSIDE_BOUNDARY);

        if (endpoint == POSITION_OUTSIDE_BOUNDARY)
          continue;

        // If skipCurrent motion returns cursor (couldn't move), skip it.
        // This happens when at word boundary with no previous word to go to.
        if (spec.skipCurrent && endpoint == cursor)
          continue;

        // Check if motion endpoint is in protected boundary region
        if (inBoundaryRegion(endpoint, lines))
          continue;

        // When fully embedded (hasLinesAbove AND hasLinesBelow), block all backward
        // line-crossing. After any line-merging deletion, the content around cursor
        // differs between isolated and fullBuffer, causing motion behavior to diverge.
        // When only hasLinesAbove (no linesBelow), backward motions from line 0 would
        // escape upward - but such motions return cursor/OUTSIDE in isolated.
        if (endpoint.line < cursor.line &&
            (editBoundary.hasLinesAbove && editBoundary.hasLinesBelow))
          continue;

        // Build range respecting cursor exclusivity
        Range range;
        if (spec.isExclusiveAtCursor) {
          // Check if cursor is at left boundary (where we can't go further left)
          int cursorContentCol = cursor.col - (cursor.line == 0 ? leftColOffset : 0);
          if (cursorContentCol > 0) {
            range = Range(endpoint, Position(cursor.line, cursor.col - 1));
          } else if (endpoint.line < cursor.line) {
            // Col at left boundary, crossing lines: delete to end of previous line only
            int prevLine = cursor.line - 1;
            int lastCol = lines[prevLine].empty()
                              ? 0
                              : static_cast<int>(lines[prevLine].size()) - 1;
            range = Range(endpoint, Position(prevLine, lastCol));
          } else {
            // Same line at left boundary: nothing to delete
            continue;
          }
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
    if (editContentLen > 0) {
      for (const auto &spec : Edit::TEXT_OBJECT_EDITS) {
        Range range = VimEndpointUtils::textObjectRange(
            cursor, lines, spec.isInner, spec.isBig,
            POSITION_OUTSIDE_BOUNDARY, POSITION_OUTSIDE_BOUNDARY);

        if (range.start == POSITION_OUTSIDE_BOUNDARY)
          continue;

        // Check if range touches protected boundary region
        if (inBoundaryRegion(range.start, lines) || inBoundaryRegion(range.end, lines))
          continue;

        // Range is already in effectiveLines coordinates
        exploreDeletion(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Line motion deletes: D, d0
    // D: on last edit line, check atLineEnd(). On other lines, always allowed.
    // d0: on first edit line, check atLineStart(). On other lines, always allowed.
    // =========================================================================
    for (const auto &spec : Edit::LINE_EDITS) {
      if (spec.forward) {
        // D: delete from cursor to end of line
        // Only check atLineEnd() on the last edit line (where suffix lives)
        if (cursor.line == lastEditLine && !editBoundary.atLineEnd()) continue;

        // Compute line-specific content bounds
        int lineLen = static_cast<int>(lines[cursor.line].size());
        int lineContentEnd = lineLen;
        if (cursor.line == lastEditLine && rightColOffset > 0) {
          lineContentEnd -= rightColOffset;
        }
        if (lineContentEnd <= 0) continue;

        int endCol = lineContentEnd - 1;
        if (endCol < cursor.col) continue;
        Range range(cursor, Position(cursor.line, endCol));
        exploreDeletion(s, range, spec.cmd, spec.keys);
      } else {
        // d0: delete from start of line to cursor (exclusive)
        // Only check atLineStart() on the first edit line (where prefix lives)
        if (cursor.line == 0 && !editBoundary.atLineStart()) continue;

        // Compute line-specific content bounds
        int lineContentStart = (cursor.line == 0) ? leftColOffset : 0;
        if (cursor.col <= lineContentStart) continue;
        Range range(Position(cursor.line, lineContentStart),
                    Position(cursor.line, cursor.col - 1));
        exploreDeletion(s, range, spec.cmd, spec.keys);
      }
    }

    // =========================================================================
    // Full line deletes: dd
    // Valid when: on an edit line AND isFullLineEditSafe() (no prefix/suffix)
    // Uses linewise deletion. If cursor lands outside edit region after
    // deletion, we add "k" to move back.
    // =========================================================================
    for (const auto &spec : Edit::FULL_LINE_EDITS) {
      // dd is safe if:
      // 1. We're on an edit line (not boundary line)
      // 2. No prefix content (atLineStart)
      // 3. No suffix content (atLineEnd)
      if (cursor.line > lastEditLine) continue;
      if (!editBoundary.isFullLineEditSafe()) continue;

      // Line must have content (can't dd an already-empty edit line)
      int lineLen = static_cast<int>(lines[cursor.line].size());
      if (lineLen == 0) continue;

      // When fully embedded (hasLinesAbove AND hasLinesBelow), dd is unsafe.
      // After dd, cursor line in isolated region (0 to N-1) doesn't match
      // fullBuffer line (M to M+N-1). Subsequent motions behave differently.
      if (editBoundary.hasLinesAbove && editBoundary.hasLinesBelow) continue;

      // When only hasLinesBelow and on last edit line, dd cursor lands below.
      if (cursor.line == lastEditLine && editBoundary.hasLinesBelow) continue;

      // When only hasLinesAbove and single line, dd leaves empty in isolated
      // but fullBuffer has lines above.
      if (cursor.line == 0 && lastEditLine == 0 && editBoundary.hasLinesAbove) continue;

      // Use linewise deletion
      exploreLinewiseDeletion(s, cursor.line, spec.cmd, spec.keys);
    }

    // =========================================================================
    // Char deletes: x, X
    // x: delete char at cursor (must be edit content, not boundary)
    // X: delete char before cursor (must be edit content, not boundary)
    // =========================================================================
    // x: only if cursor is on edit content (not on boundary char)
    if (editContentLen > 0 && cursor.col >= contentStart && cursor.col < contentEnd) {
      // Check if this is the last edit char - after x, cursor would land on boundary
      bool isLastEditChar = (cursor.col == contentEnd - 1);
      bool wouldLandOnBoundary = isLastEditChar && rightColOffset > 0;

      if (wouldLandOnBoundary) {
        // Only allow if this x reaches goal (deletes last char of single-char content)
        // This is safe when editContentLen == 1 (deleting the only remaining edit char)
        if (editContentLen == 1) {
          Range range(cursor, cursor);
          exploreDeletion(s, range, "x", Deletion::CHAR.at("x"));
        }
      } else {
        Range range(cursor, cursor);
        exploreDeletion(s, range, "x", Deletion::CHAR.at("x"));
      }
    }

    // X: delete char before cursor (must be edit content)
    if (cursor.col > contentStart) {
      Position before(cursor.line, cursor.col - 1);
      // Ensure we're not deleting a boundary char
      if (!inBoundaryRegion(before, lines)) {
        Range range(before, before);
        exploreDeletion(s, range, "X", Deletion::CHAR.at("X"));
      }
    }

    // =========================================================================
    // Vertical navigation: j, k
    // When on an empty line, allow j/k to navigate to other lines.
    // This enables reaching lines that can then be deleted with dd or collapsed
    // with <BS>/<Del> in insert mode.
    // TEMPORARILY DISABLED: j/k sequences cause divergence in stress tests.
    // TODO: Fix j/k simulation to match vim's targetCol behavior.
    // =========================================================================
    if (false && editContentLen == 0) {
      // j: move down (if not on last edit line)
      if (cursor.line < lastEditLine) {
        EditState newState = s;
        Position newPos(cursor.line + 1, 0);
        // Clamp col to line length
        int nextLineLen = static_cast<int>(lines[newPos.line].size());
        if (nextLineLen > 0) {
          newPos.col = min(cursor.col, nextLineLen - 1);
        }
        newState.setPos(newPos);
        newState.appendToSeq("j");
        PhysicalKeys jKeys = {Key::Key_J};
        newState.updateEffort(jKeys, config);
        // Heuristic stays same (no content deleted)
        newState.updateCost(newState.getEffort() + heuristic(newState.getLines()));
        exploreNewState(std::move(newState));
      }

      // k: move up (if not on first edit line)
      if (cursor.line > 0) {
        EditState newState = s;
        Position newPos(cursor.line - 1, 0);
        // Clamp col to line length
        int prevLineLen = static_cast<int>(lines[newPos.line].size());
        if (prevLineLen > 0) {
          newPos.col = min(cursor.col, prevLineLen - 1);
        }
        newState.setPos(newPos);
        newState.appendToSeq("k");
        PhysicalKeys kKeys = {Key::Key_K};
        newState.updateEffort(kKeys, config);
        // Heuristic stays same (no content deleted)
        newState.updateCost(newState.getEffort() + heuristic(newState.getLines()));
        exploreNewState(std::move(newState));
      }
    }
  }

  return result;
}
