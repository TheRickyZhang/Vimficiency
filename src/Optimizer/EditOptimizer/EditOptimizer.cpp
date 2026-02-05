// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"
#include "EditSearchContext.h"
#include "Keyboard/KeyboardModel.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"

#include "Editor/NavContext.h"
#include "Keyboard/CharToKeys.h"
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
  for(size_t i = 0; i < editResult.results.size(); i++) {
    const auto& res = editResult.results[i];
    os << (res.isValid() ? res.getSequenceString() : "_");

    if(i < editResult.results.size() - 1) os << " ";
  }
  os << "\n";
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


// Convert delete command to change equivalent
// Returns the change command string, or empty string if no mapping exists
pair<string, PhysicalKeys> deleteToChange(const string& deleteCmd) {
  if (deleteCmd == "D")
    return {"C", {Key::Key_Shift, Key::Key_C}};
  if (deleteCmd == "dd")
    return {"cc", {Key::Key_C, Key::Key_C}};
  if (deleteCmd[0] == 'd') {
    string s = deleteCmd.substr(1);
    return {"c" + s, PhysicalKeys{Key::Key_C}.append(globalTokenizer().tokenize(s))};
  }
  if (deleteCmd == "x")
    return {"s", {Key::Key_S}};
  if (deleteCmd == "X")
    return {"hs", {Key::Key_H, Key::Key_S}};
  assert(false && "deleteToChange not supported");
  return {"", {}};
}


// Build the typed content string from goalLines
pair<string, PhysicalKeys> buildTypedCommands(const Lines &goalLines) {
  string str;
  PhysicalKeys keys;
  for (size_t i = 0; i < goalLines.size(); i++) {
    str += goalLines[i];
    for (int c : goalLines[i]) {
      keys.append(CHAR_TO_KEYS.at(c));
    }
    if (i < goalLines.size() - 1) {
      str += "<CR>";
      keys.push_back(Key::Key_Enter);
    }
  }
  str += "<Esc>";
  keys.push_back(Key::Key_Esc);
  return {str, keys};
}

} // anonymous namespace

// replacement strategy for same-length transformations
// Returns result for position 0 only (the only position ever consumed)
optional<Result> tryReplacement(const string &deleted, const string &inserted,
                                const Config &config) {
  assert(deleted.size() == inserted.size());
  assert(deleted != inserted);

  // Find all differing positions
  vector<int> diff;
  for (size_t i = 0; i < deleted.size(); i++) {
    if (deleted[i] != inserted[i]) {
      diff.push_back(static_cast<int>(i));
    }
  }

  int firstDiff = diff[0];

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

  // Build result for position 0 only
  // If firstDiff > 0, we need to move right to reach the first change
  string prefix;
  RunningEffort runningEffort;
  if (firstDiff > 0) {
    if (firstDiff <= 2) {
      prefix = string(firstDiff, 'l');
      runningEffort.append(PhysicalKeys(firstDiff, Key::Key_L), config);
    } else {
      prefix = to_string(firstDiff) + "l";
      runningEffort.append({CharMappings::digitsArr[firstDiff], Key::Key_L}, config);
    }
  }

  PhysicalKeys keys = globalTokenizer().tokenize(seq);
  double effort = runningEffort.append(keys, config);
  return Result(prefix + seq, effort);
}

// =============================================================================
// optimizeEdit - main entry point
// =============================================================================

EditResult
EditOptimizer::optimizeEdit(const Lines &initialLines, const Lines &goalLines,
                            EditBoundary editBoundary,
                            EditOptimizerParams params) {
  assert(initialLines != goalLines);
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  // Delegate to optimizePureDeletion for pure deletion (goalLines empty)
  if (allLinesEmpty(goalLines)) {
    return optimizePureDeletion(initialLines, editBoundary, params);
  }

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(initialLines, editBoundary, params, config);
  ctx.initStartingPositions(initialLines);

  // Local aliases for goal checking
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  const string preSuf = pre + suf;

  EditResult result(ctx.totalPositions);

  // Check if replacement strategy is applicable (same-length, single-line)
  optional<Result> replacementResult;
  if (initialLines.size() == 1 && goalLines.size() == 1 &&
      initialLines[0].size() == goalLines[0].size() && !initialLines[0].empty()) {
    replacementResult = tryReplacement(initialLines[0], goalLines[0], config);
  }

  // Precompute typed content for goal state
  auto [typedStr, typedKeys] = buildTypedCommands(goalLines);

  // Goal check for regular edit: accepts multi-line (for collapse via <BS>/<Del>)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.size() == 1) return lines[0] == preSuf;
    if (lines[0] != pre) return false;
    if (lines.back() != suf) return false;
    for (size_t i = 1; i < lines.size() - 1; i++) {
      if (!lines[i].empty()) return false;
    }
    return true;
  };

  // Deletion handler: apply deletion, check goal, store result or continue search
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const char* deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterDeletion(range);
    const Lines &lines = newState.getLines();

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.results[idx].isValid()) return;

      auto [changeCmd, changeKeys] = deleteToChange(deleteCmd);
      auto [collapseSeq, collapseKeys] =
          buildCollapseSequence(static_cast<int>(lines.size()), newState.getPos().line);

      string seqStr = newState.getSeq() + changeCmd + collapseSeq + typedStr;
      RunningEffort effort = newState.getRunningEffort();
      effort.append(changeKeys, config);
      effort.append(collapseKeys, config);
      double totalEffort = effort.append(typedKeys, config);

      result.results[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    newState.recordSearch(deleteCmd, deleteKeys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // Linewise handler: search with dd, record result as cc + collapseSeq + typedStr
  // The cc conversion accounts for the empty line that cc leaves (vs dd which removes it)
  auto exploreLinewise = [&](const EditState &base, int line,
                             const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterLinewiseDeletion(line);
    const Lines &lines = newState.getLines();

    // For search continuation: adjust cursor if it escaped below edit region
    string searchCmdSeq = deleteCmd;
    PhysicalKeys searchCmdKeys = deleteKeys;
    Position pos = newState.getPos();
    int lastValidLine = static_cast<int>(lines.size()) - 1;
    if (editBoundary.hasLinesBelow() && lastValidLine >= 0 && pos.line > lastValidLine) {
      searchCmdSeq += "k";
      searchCmdKeys.push_back(Key::Key_K);
      pos.line = lastValidLine;
      // k motion preserves targetCol (sticky column behavior)
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0 :
                 min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      newState.setPos(pos);
    }

    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      if (result.results[idx].isValid()) return;

      // Convert dd -> cc: the cc equivalent has one more line (the empty line it leaves)
      // Cursor position for cc would be at `line` (the cleared line, not the dd cursor)
      static const PhysicalKeys ccKeys = {Key::Key_C, Key::Key_C};
      auto [collapseSeq, collapseKeys] =
          buildCollapseSequence(static_cast<int>(lines.size()) + 1, line);

      string seqStr = newState.getSeq() + "cc" + collapseSeq + typedStr;
      RunningEffort effort = newState.getRunningEffort();
      effort.append(ccKeys, config);
      effort.append(collapseKeys, config);
      double totalEffort = effort.append(typedKeys, config);

      result.results[idx] = Result(seqStr, totalEffort);
      ctx.resultsFound++;
      return;
    }

    // Continue search with dd state
    newState.recordSearch(searchCmdSeq, searchCmdKeys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // Main search loop
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    // Join handler: J/gJ merges current line with next
    auto exploreJoin = [&](const EditState& base, bool addSpace,
                           const char* joinCmd, const PhysicalKeys& joinKeys) {
      EditState newState = base.afterJoin(addSpace);
      const Lines& lines = newState.getLines();

      if (isGoalReached(lines)) {
        int idx = newState.getStartIndex();
        if (result.results[idx].isValid()) return;

        // For optimizeEdit, we need to enter insert mode to type content
        // J doesn't enter insert mode, so we need to use a change command after
        // For now, just continue search - J is more useful for pure deletion
      }

      newState.recordSearch(joinCmd, joinKeys,
                            ctx.computePriority(newState.getEffort(), lines), config);
      ctx.exploreNewState(std::move(newState));
    };

    // Explore all deletions (characterwise + linewise) via EditSearchContext
    ctx.exploreAllDeletions(
      s,
      [&](const Range& range, const char* cmd, const PhysicalKeys& keys) {
        exploreDeletion(s, range, cmd, keys);
      },
      [&](int line, const char* cmd, const PhysicalKeys& keys) {
        exploreLinewise(s, line, cmd, keys);
      },
      [&](const Position& newPos, const char* cmd, const PhysicalKeys& keys) {
        // Pure cursor movement - no buffer change, no goal check possible
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(cmd, keys,
                              ctx.computePriority(newState.getEffort(), newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, const char* cmd, const PhysicalKeys& keys) {
        exploreJoin(s, addSpace, cmd, keys);
      }
    );
  }

  // Merge replacement result at position 0 if it's better
  if (replacementResult.has_value()) {
    if (!result.results[0].isValid() ||
        replacementResult->keyCost < result.results[0].keyCost) {
      result.results[0] = *replacementResult;
    }
  }

  result.stats = ctx.getStats();
  return result;
}

// =============================================================================
// optimizePureDeletion - simplified deletion-only optimization
// =============================================================================

EditResult
EditOptimizer::optimizePureDeletion(const Lines &initialLines,
                                    EditBoundary editBoundary,
                                    EditOptimizerParams params) {
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");

  // Create search context (handles effectiveLines, offsets, search state)
  EditSearchContext ctx(initialLines, editBoundary, params, config);
  ctx.initStartingPositions(initialLines);

  // Local alias for goal checking
  const string preSuf = editBoundary.prefix() + editBoundary.suffix();

  EditResult result(ctx.totalPositions);
  vector<Result>& results = result.results;

  // Goal check for pure deletion: only single-line goals accepted
  // (can't collapse multiple empty lines without insert mode)
  auto isGoalReached = [&](const Lines &lines) -> bool {
    if (lines.size() != 1) return false;
    return lines[0] == preSuf;
  };

  // Deletion handler: output delete command directly (no change conversion)
  auto exploreDeletion = [&](const EditState &base, const Range &range,
                             const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterDeletion(range);
    const Lines &lines = newState.getLines();

    // Check goal immediately - multi-source A* needs this since state key doesn't include startIndex
    // Update if better since exploration order doesn't guarantee optimality per-source
    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      RunningEffort effort = newState.getRunningEffort();
      double totalEffort = effort.append(deleteKeys, config);

      bool isNew = !results[idx].isValid();
      if (isNew || totalEffort < results[idx].keyCost) {
        results[idx] = Result(newState.getSeq() + deleteCmd, totalEffort);
        if (isNew) ctx.resultsFound++;
      }
      return;
    }

    newState.recordSearch(deleteCmd, deleteKeys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // Linewise handler: record dd directly (pure deletion, no cc conversion)
  auto exploreLinewise = [&](const EditState &base, int line,
                             const char *deleteCmd, const PhysicalKeys &deleteKeys) {
    EditState newState = base.afterLinewiseDeletion(line);
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
      // k motion preserves targetCol (sticky column behavior)
      pos.clampColPreservingTarget(lines[pos.line].empty() ? 0 :
                 min(pos.targetCol, static_cast<int>(lines[pos.line].size()) - 1));
      newState.setPos(pos);
    }

    // Check goal immediately - multi-source A* needs this since state key doesn't include startIndex
    // Update if better since exploration order doesn't guarantee optimality per-source
    if (isGoalReached(lines)) {
      int idx = newState.getStartIndex();
      RunningEffort effort = newState.getRunningEffort();
      double totalEffort = effort.append(cmdKeys, config);

      bool isNew = !results[idx].isValid();
      if (isNew || totalEffort < results[idx].keyCost) {
        results[idx] = Result(newState.getSeq() + cmdSeq, totalEffort);
        if (isNew) ctx.resultsFound++;
      }
      return;
    }

    newState.recordSearch(cmdSeq, cmdKeys,
                          ctx.computePriority(newState.getEffort(), lines), config);
    ctx.exploreNewState(std::move(newState));
  };

  // Main search loop
  while (ctx.shouldContinue()) {
    ctx.iterations++;

    auto maybeState = ctx.getNextValidState();
    if (!maybeState) continue;
    EditState s = std::move(*maybeState);

    // Early stopping: skip if this startIndex already has a result
    // Since A* explores in cost order, first result found is optimal
    if (results[s.getStartIndex()].isValid()) continue;

    // Join handler for pure deletion: J/gJ merges lines without adding new content
    auto exploreJoin = [&](const EditState& base, bool addSpace,
                           const char* joinCmd, const PhysicalKeys& joinKeys) {
      EditState newState = base.afterJoin(addSpace);
      const Lines& lines = newState.getLines();

      // Check goal immediately - multi-source A* needs this since state key doesn't include startIndex
      // Update if better since exploration order doesn't guarantee optimality per-source
      if (isGoalReached(lines)) {
        int idx = newState.getStartIndex();
        RunningEffort effort = newState.getRunningEffort();
        double totalEffort = effort.append(joinKeys, config);

        bool isNew = !results[idx].isValid();
        if (isNew || totalEffort < results[idx].keyCost) {
          results[idx] = Result(newState.getSeq() + joinCmd, totalEffort);
          if (isNew) ctx.resultsFound++;
        }
        return;
      }

      newState.recordSearch(joinCmd, joinKeys,
                            ctx.computePriority(newState.getEffort(), lines), config);
      ctx.exploreNewState(std::move(newState));
    };

    // Explore all deletions (characterwise + linewise) via EditSearchContext
    ctx.exploreAllDeletions(
      s,
      [&](const Range& range, const char* cmd, const PhysicalKeys& keys) {
        exploreDeletion(s, range, cmd, keys);
      },
      [&](int line, const char* cmd, const PhysicalKeys& keys) {
        exploreLinewise(s, line, cmd, keys);
      },
      [&](const Position& newPos, const char* cmd, const PhysicalKeys& keys) {
        // Pure cursor movement - no buffer change, no goal check possible
        EditState newState = s;
        newState.setPos(newPos);
        newState.recordSearch(cmd, keys,
                              ctx.computePriority(newState.getEffort(), newState.getLines()), config);
        ctx.exploreNewState(std::move(newState));
      },
      [&](bool addSpace, const char* cmd, const PhysicalKeys& keys) {
        exploreJoin(s, addSpace, cmd, keys);
      }
    );
  }

  // Try visual mode deletion: v{motion}d from first content position to last
  // This only applies to position 0 (first content position)
  if (ctx.effectiveLines.size() > 1 ||
      static_cast<int>(ctx.effectiveLines[0].size()) > ctx.leftColOffset + ctx.rightColOffset) {
    // First content position
    Position firstPos(0, ctx.leftColOffset);

    // Last content position
    int lastLine = ctx.effectiveLines.lastLine();
    int lastCol = static_cast<int>(ctx.effectiveLines[lastLine].size()) - 1 - ctx.rightColOffset;
    Position lastPos(lastLine, max(0, lastCol));

    // Only try visual if there's actual content to select
    if (lastPos > firstPos || (lastPos.line == firstPos.line && lastPos.col > firstPos.col)) {
      MotionOptimizer motionOpt(config);
      NavContext navCtx;

      // Find best motion from first to last
      // Use params.motionLinePadding* for consistency with other optimizer calls
      // See docs/optimizer/buffer-slicing.md for padding rationale
      auto [motionResults, motionStats] = motionOpt.optimize(
          ctx.effectiveLines,
          firstPos,
          lastPos,
          MotionOptimizerParams{}
              .withLinePaddingAbove(params.motionLinePaddingAbove)
              .withLinePaddingBelow(params.motionLinePaddingBelow)
      );

      if (!motionResults.empty() && motionResults[0].isValid()) {
        // Build visual mode sequence: v + motion + d
        string visualSeq = "v" + motionResults[0].getSequenceString() + "d";

        // Calculate effort: v + motion + d
        RunningEffort effort;
        static const PhysicalKeys vKey = {Key::Key_V};
        static const PhysicalKeys dKey = {Key::Key_D};
        effort.append(vKey, config);
        effort.append(globalTokenizer().tokenize(motionResults[0].getSequenceString()), config);
        double totalEffort = effort.append(dKey, config);

        // Compare with existing result[0] and use better one
        if (!results[0].isValid() || totalEffort < results[0].keyCost) {
          results[0] = Result(visualSeq, totalEffort);
        }
      }
    }
  }

  result.stats = ctx.getStats();
  return result;
}
