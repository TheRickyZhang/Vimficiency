// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"
#include "EditSearchContext.h"

#include "Keyboard/CharToKeys.h"
#include "Keyboard/EditToKeys.h"
#include "Keyboard/MotionToKeys.h"
#include "State/RunningEffort.h"

#include <algorithm>
#include <optional>

using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

ostream& operator<<(ostream& os, const EditResult& editResult) {
  os << "typeAllResults: ";
  for(int i = 0; i < editResult.typeAllResults.size(); i++) {
    const auto& res = editResult.typeAllResults[i];
    os << (res.isValid() ? res.getSequenceString() : "_");

    if(i < editResult.typeAllResults.size()) os << " ";
    else os << "\n";
  }
  if(!editResult.replaceResults.empty()) {
    os << "replaecmentResults: ";
    for(int i = 0; i < editResult.replaceResults.size(); i++) {
      const auto& res = editResult.replaceResults[i];
      os << (res.isValid() ? res.getSequenceString() : "_");
      if(i < editResult.typeAllResults.size()) os << " ";
      else os << "\n";
    }  
    os << "replacementEnd: " << editResult.replaceEnd << "\n";
  }
  return os;
}

namespace {

// Build collapse sequence to merge multi-line goal state into single line.
// <BS> joins current line with previous, <Del> joins with next.
pair<string, PhysicalKeys> buildCollapseSequence(int totalLines, int cursorLine) {
  string seq;
  PhysicalKeys keys;

  if (totalLines > 1) {
    int linesBefore = cursorLine;
    int linesAfter = totalLines - 1 - cursorLine;

    for (int i = 0; i < linesBefore; i++) {
      seq += "<BS>";
      keys.push_back(Key::Key_Backspace);
    }
    for (int i = 0; i < linesAfter; i++) {
      seq += "<Del>";
      keys.push_back(Key::Key_Delete);
    }
  }

  return {seq, keys};
}

// Compute remaining edit content for heuristic (excluding prefix/suffix)
double computeRemainingHeuristic(const Lines &lines, int leftColOffset,
                                  int rightColOffset) {
  double remaining = 0;
  int lastLine = static_cast<int>(lines.size()) - 1;
  for (int i = 0; i <= lastLine; i++) {
    int start = (i == 0) ? leftColOffset : 0;
    int end = static_cast<int>(lines[i].size());
    if (i == lastLine && rightColOffset > 0) {
      end -= rightColOffset;
    }
    remaining += max(0, end - start);
    if (i < lastLine) {
      remaining += 1; // Newline
    }
  }
  return remaining;
}

// Check if buffer is effectively empty (all lines are empty strings)
bool allLinesEmpty(const Lines &lines) {
  if (lines.empty()) return true;
  for (const auto &line : lines) {
    if (!line.empty()) return false;
  }
  return true;
}

int getEffectivePositionCount(const vector<string> lines) {
  int res = 0;
  for (const auto &line : lines) {
    res += line.empty() ? 1 : static_cast<int>(line.size());
  }
  return res;
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

// How many characters remaining, including new lines
double EditOptimizer::heuristic(const Lines &lines) const {
  double total = 0;
  for (size_t i = 0; i < lines.size(); i++) {
    total += lines[i].size();
    if (i < lines.size() - 1)
      total += 1;
  }
  return total;
}

// replacement strategy for same-length transformations
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
      int diff = firstDiff - i;
      if(diff <= 2) {
        temp.first = string(diff, 'l');
        temp.second.append(PhysicalKeys(diff, Key::Key_L), config);
      } else {
        temp.second.append({CharMappings::digitsArr[diff], Key::Key_L}, config);
      }
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

      if (dist <= 2) {
        seq += string(dist, 'l');
      } else {
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
  assert(startLines != endLines);
  assert(!startLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  // Delegate to optimizePureDeletion for pure deletion (endLines empty)
  if (allLinesEmpty(endLines)) {
    vector<Result> deletionResults = optimizePureDeletion(startLines, editBoundary, paramsOverride);
    int n = static_cast<int>(deletionResults.size());
    EditResult result(n, {}, -1);
    result.typeAllResults = std::move(deletionResults);
    return result;
  }

  const OptimizerParams &params = paramsOverride.value_or(defaultParams);

  // Get replacement results
  vector<Result> replacementResults;
  int lastReplacementPos = -1;
  if (startLines.size() == 1 && endLines.size() == 1 &&
      startLines[0].size() == endLines[0].size() && !startLines[0].empty()) {
    tryReplacement(startLines[0], endLines[0], config, lastReplacementPos,
                   replacementResults);
  }

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(startLines, editBoundary, params, config);
  ctx.initStartingPositions(startLines);

  EditResult result(ctx.totalPositions, replacementResults, lastReplacementPos);

  // Precompute typed content for goal state
  auto [typedStr, typedKeys] = buildTypedCommands(endLines);

  // Goal check for regular edit: accepts multi-line (for collapse via <BS>/<Del>)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.size() == 1) return lines[0] == ctx.preSuf;
    if (lines[0] != ctx.pre) return false;
    if (lines.back() != ctx.suf) return false;
    for (size_t i = 1; i < lines.size() - 1; i++) {
      if (!lines[i].empty()) return false;
    }
    return true;
  };

  // Deletion handler: apply deletion, check goal, store result or continue search
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base;
    newState.applyDeletion(range);
    const Lines &lines = newState.getLines();

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.typeAllResults[idx].isValid()) return;

      string changeCmd = deleteToChange(deleteCmd);
      auto [collapseSeq, collapseKeys] =
          buildCollapseSequence(static_cast<int>(lines.size()), newState.getPos().line);

      string seqStr = newState.getSeq() + changeCmd + collapseSeq + typedStr;
      PhysicalKeys changeKeys = globalTokenizer().tokenize(changeCmd);
      RunningEffort effort = newState.getRunningEffort();
      effort.append(changeKeys, config);
      effort.append(collapseKeys, config);
      double totalEffort = effort.append(typedKeys, config);

      result.typeAllResults[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    newState.appendToSeq(deleteCmd);
    newState.updateEffort(deleteKeys, config);
    newState.updateCost(newState.getEffort() + ctx.computeRemainingHeuristic(lines));
    ctx.exploreNewState(std::move(newState));
  };

  // Linewise deletion (dd) - only for pure deletion within optimizeEdit
  auto exploreLinewiseDeletion = [&](const EditState &base, int line,
                                     const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base;
    newState.applyLinewiseDeletion(line);
    const Lines &lines = newState.getLines();

    string cmdSeq = deleteCmd;
    PhysicalKeys cmdKeys = deleteKeys;

    // Adjust cursor if it escaped below edit region
    Position pos = newState.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    if (editBoundary.hasLinesBelow() && lastValidLine >= 0 && pos.line > lastValidLine) {
      cmdSeq += "k";
      cmdKeys.push_back(Key::Key_K);
      pos.line = lastValidLine;
      pos.col = lines[pos.line].empty() ? 0 :
                min(pos.col, static_cast<int>(lines[pos.line].size()) - 1);
      newState.setPos(pos);
    }

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.typeAllResults[idx].isValid()) return;

      string seqStr = newState.getSeq() + cmdSeq;
      RunningEffort effort = newState.getRunningEffort();
      double totalEffort = effort.append(cmdKeys, config);
      result.typeAllResults[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    newState.appendToSeq(cmdSeq.c_str());
    newState.updateEffort(cmdKeys, config);
    newState.updateCost(newState.getEffort() + ctx.computeRemainingHeuristic(lines));
    ctx.exploreNewState(std::move(newState));
  };

  // Linewise change for cc - clears line content, enters insert mode
  auto exploreLinewiseChange = [&](const EditState &base, int line,
                                   const char *changeCmd, const PhysicalKeys &changeKeys) {
    EditState newState = base;
    newState.applyLinewiseChange(line);
    const Lines &lines = newState.getLines();

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.typeAllResults[idx].isValid()) return;

      auto [collapseSeq, collapseKeys] =
          buildCollapseSequence(static_cast<int>(lines.size()), newState.getPos().line);

      string seqStr = newState.getSeq() + changeCmd + collapseSeq + typedStr;
      RunningEffort effort = newState.getRunningEffort();
      effort.append(changeKeys, config);
      effort.append(collapseKeys, config);
      double totalEffort = effort.append(typedKeys, config);

      result.typeAllResults[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    newState.appendToSeq(changeCmd);
    newState.updateEffort(changeKeys, config);
    newState.updateCost(newState.getEffort() + ctx.computeRemainingHeuristic(lines));
    ctx.exploreNewState(std::move(newState));
  };

  // Main search loop
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.popNextState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    const Lines &lines = s.getLines();
    Position cursor = s.getPos();

    // Explore all characterwise deletions via EditSearchContext
    ctx.exploreAllDeletions(s, [&](const Range& range, const char* cmd, const PhysicalKeys& keys) {
      exploreDeletion(s, range, cmd, keys);
    });

    // Full line operations (dd/cc) - handled separately since logic differs
    auto [contentStart, contentEnd, editContentLen, lastEditLine] =
        ctx.computeContentBounds(lines, cursor);

    if (editBoundary.isFullLineEditSafe() && cursor.line <= lastEditLine) {
      int lineLen = static_cast<int>(lines[cursor.line].size());
      if (lineLen > 0) {
        bool isPureDeletion = (typedStr == "<Esc>");

        if (isPureDeletion) {
          // Use dd for pure deletion - block in divergent scenarios
          bool blocked = false;
          if (editBoundary.hasLinesAbove() && editBoundary.hasLinesBelow()) {
            if (static_cast<int>(lines.size()) > 1) blocked = true;
          }
          if (cursor.line > 0 && editBoundary.hasLinesBelow()) blocked = true;

          if (!blocked) {
            for (const auto &spec : Edit::FULL_LINE_EDITS) {
              exploreLinewiseDeletion(s, cursor.line, spec.cmd, spec.keys);
            }
          }
        } else {
          // Use cc for content typing - cursor stays on line, no divergence issues
          static const PhysicalKeys ccKeys = {Key::Key_C, Key::Key_C};
          exploreLinewiseChange(s, cursor.line, "cc", ccKeys);
        }
      }
    }

    // Note: char deletes (x, X) are handled by ctx.exploreAllDeletions()
  }

  return result;
}

// =============================================================================
// optimizePureDeletion - simplified deletion-only optimization
// =============================================================================

vector<Result>
EditOptimizer::optimizePureDeletion(const Lines &startLines,
                                    EditBoundary editBoundary,
                                    const optional<OptimizerParams> &paramsOverride) {
  assert(!startLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  const OptimizerParams &params = paramsOverride.value_or(defaultParams);

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(startLines, editBoundary, params, config);
  ctx.initStartingPositions(startLines);

  vector<Result> results(ctx.totalPositions);

  // Goal check for pure deletion: only single-line goals accepted
  // (can't collapse multiple empty lines without insert mode)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.size() != 1) return false;
    return lines[0] == ctx.preSuf;
  };

  // Deletion handler: output delete command directly (no change conversion)
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base;
    newState.applyDeletion(range);
    const Lines &lines = newState.getLines();

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (results[idx].isValid()) return;

      string seqStr = newState.getSeq() + deleteCmd;
      RunningEffort effort = newState.getRunningEffort();
      double totalEffort = effort.append(deleteKeys, config);

      results[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    newState.appendToSeq(deleteCmd);
    newState.updateEffort(deleteKeys, config);
    newState.updateCost(newState.getEffort() + ctx.computeRemainingHeuristic(lines));
    ctx.exploreNewState(std::move(newState));
  };

  // Linewise deletion (dd)
  auto exploreLinewiseDeletion = [&](const EditState &base, int line,
                                     const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base;
    newState.applyLinewiseDeletion(line);
    const Lines &lines = newState.getLines();

    string cmdSeq = deleteCmd;
    PhysicalKeys cmdKeys = deleteKeys;

    // Adjust cursor if it escaped below edit region
    Position pos = newState.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    if (editBoundary.hasLinesBelow() && lastValidLine >= 0 && pos.line > lastValidLine) {
      cmdSeq += "k";
      cmdKeys.push_back(Key::Key_K);
      pos.line = lastValidLine;
      pos.col = lines[pos.line].empty() ? 0 :
                min(pos.col, static_cast<int>(lines[pos.line].size()) - 1);
      newState.setPos(pos);
    }

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (results[idx].isValid()) return;

      string seqStr = newState.getSeq() + cmdSeq;
      RunningEffort effort = newState.getRunningEffort();
      double totalEffort = effort.append(cmdKeys, config);
      results[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    newState.appendToSeq(cmdSeq.c_str());
    newState.updateEffort(cmdKeys, config);
    newState.updateCost(newState.getEffort() + ctx.computeRemainingHeuristic(lines));
    ctx.exploreNewState(std::move(newState));
  };

  // Main search loop
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.popNextState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    const Lines &lines = s.getLines();
    Position cursor = s.getPos();

    // Explore all characterwise deletions via EditSearchContext
    ctx.exploreAllDeletions(s, [&](const Range& range, const char* cmd, const PhysicalKeys& keys) {
      exploreDeletion(s, range, cmd, keys);
    });

    // Full line deletion (dd) - handled separately
    auto [contentStart, contentEnd, editContentLen, lastEditLine] =
        ctx.computeContentBounds(lines, cursor);

    if (editBoundary.isFullLineEditSafe() && cursor.line <= lastEditLine) {
      int lineLen = static_cast<int>(lines[cursor.line].size());
      if (lineLen > 0) {
        bool blocked = false;
        if (editBoundary.hasLinesAbove() && editBoundary.hasLinesBelow()) {
          if (static_cast<int>(lines.size()) > 1) blocked = true;
        }
        if (cursor.line > 0 && editBoundary.hasLinesBelow()) blocked = true;

        if (!blocked) {
          for (const auto &spec : Edit::FULL_LINE_EDITS) {
            exploreLinewiseDeletion(s, cursor.line, spec.cmd, spec.keys);
          }
        }
      }
    }
  }

  return results;
}
