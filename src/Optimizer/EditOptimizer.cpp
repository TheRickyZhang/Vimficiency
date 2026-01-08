// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include "EditBoundary.h"
#include "Editor/Edit.h"
#include "Editor/NavContext.h"
#include "Keyboard/CharToKeys.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "State/EditState.h"
#include "State/RunningEffort.h"
#include "Utils/Debug.h"

#include <queue>
#include <unordered_map>
#include <optional>
#include <climits>

using namespace std;

// =============================================================================
// Heuristic for A* search
// =============================================================================

double EditOptimizer::heuristic(const EditState& s, const OptimizerParams& params) const {
  // For deletion: total characters remaining + line count + mode penalty
  double total = 0;
  for (const auto& line : s.lines) {
    total += line.size();
  }
  if (s.lines.size() > 1) {
    total += s.lines.size() - 1;  // Newlines to delete
  }
  if (s.mode != Mode::Insert) {
    total += 1;  // Need to enter insert mode
  }
  return total;
}

// =============================================================================
// optimizeEdit - uses deletion search as foundation
// =============================================================================

EditResult EditOptimizer::optimizeEdit(const Lines& sourceLines,
                                       const Lines& endLines,
                                       const EditBoundary& boundary,
                                       const optional<OptimizerParams>& paramsOverride) {
  int n = sourceLines.size();
  int m = endLines.size();

  // For now: n = number of starting positions (rows), m columns (last column = m-1)
  // We store deletion results in adj[row][m-1] for each starting row
  // Each row can have multiple columns (one per character position)

  // Calculate max columns across all source lines
  int maxCols = 1;
  for (const auto& line : sourceLines) {
    maxCols = max(maxCols, (int)line.size());
  }
  if (maxCols == 0) maxCols = 1;

  // Result dimensions: n rows x maxCols columns
  // adj[r][c] = optimal deletion from position (r, c)
  EditResult res(n == 0 ? 1 : n, maxCols);

  // Handle empty source - nothing to delete
  if (sourceLines.empty()) {
    res.adj[0][0] = Result("", 0);  // No-op
    return res;
  }

  // Run deletion search with boundary constraints
  DeletionResult delResult = optimizeDeletion(sourceLines, boundary);

  // Copy results into EditResult structure
  for (int r = 0; r < n; r++) {
    int cols = sourceLines[r].empty() ? 1 : sourceLines[r].size();
    for (int c = 0; c < cols && c < maxCols; c++) {
      res.adj[r][c] = delResult.at(r, c);
    }
  }

  return res;
}

// =============================================================================
// Deletion Search - A* to find optimal ways to clear buffer from any position
// =============================================================================

// Operations to explore in deletion search
static const vector<string> DELETION_OPS = {
  // Line operations
  "dd", "cc", "S",
  // Word deletions
  "dw", "dW", "de", "dE", "db", "dB", "dge", "dgE",
  // Word changes (enters insert mode)
  "cw", "cW", "ce", "cE", "cb", "cB", "cge", "cgE",
  // Line-partial operations
  "D", "d$", "C", "c$", "d0", "c0", "d^", "c^",
  // Character operations
  "x", "s",
  // Text objects
  "diw", "daw", "diW", "daW",
  "ciw", "caw", "ciW", "caW",
  // Navigation (no buffer change, but needed to reach different positions)
  "j", "k", "h", "l",
  "w", "W", "b", "B", "e", "E", "ge", "gE",
  "0", "^", "$",
};

// Check if state is a goal state for deletion search
// If boundary has lines above/below, we can only clear to single empty line (can't delete all lines)
static bool isDeletionGoal(const EditState& s, const EditBoundary& boundary) {
  // Must be in insert mode
  if (s.mode != Mode::Insert) return false;

  // If there are lines outside the region, we can only reduce to single empty line
  bool canDeleteAllLines = !boundary.hasLinesAbove && !boundary.hasLinesBelow;

  if (canDeleteAllLines) {
    // Can reach truly empty buffer
    if (s.lines.empty()) return true;
  }

  // Single empty line is always a valid goal
  if (s.lines.size() == 1 && s.lines[0].empty()) return true;

  // For partial-line regions, we can't reduce line count (no dd/J allowed),
  // so goal is "all lines empty" - preserves line structure
  bool isPartialLineRegion = !boundary.startsAtLineStart || !boundary.endsAtLineEnd;
  if (isPartialLineRegion && s.lines.size() > 1) {
    bool allEmpty = true;
    for (const auto& line : s.lines) {
      if (!line.empty()) {
        allEmpty = false;
        break;
      }
    }
    if (allEmpty) return true;
  }

  return false;
}

// Heuristic: total characters remaining in buffer
static double deletionHeuristic(const EditState& s) {
  double total = 0;
  for (const auto& line : s.lines) {
    total += line.size();
  }
  // Add cost for lines (need to delete newlines too)
  if (s.lines.size() > 1) {
    total += s.lines.size() - 1;
  }
  // Not in insert mode yet - add small cost
  if (s.mode != Mode::Insert) {
    total += 1;
  }
  return total;
}

// Map operation string to ForwardEdit enum
static optional<ForwardEdit> getForwardEditType(const string& op) {
  if (op == "x") return ForwardEdit::CHAR;
  if (op == "dw" || op == "cw") return ForwardEdit::WORD_TO_START;
  if (op == "de" || op == "ce") return ForwardEdit::WORD_TO_END;
  if (op == "dW" || op == "cW") return ForwardEdit::BIG_WORD_TO_START;
  if (op == "dE" || op == "cE") return ForwardEdit::BIG_WORD_TO_END;
  if (op == "D" || op == "C" || op == "d$" || op == "c$") return ForwardEdit::LINE_TO_END;
  return nullopt;
}

// Map operation string to BackwardEdit enum
static optional<BackwardEdit> getBackwardEditType(const string& op) {
  if (op == "X") return BackwardEdit::CHAR;
  if (op == "db" || op == "cb") return BackwardEdit::WORD_TO_START;
  if (op == "dge" || op == "cge") return BackwardEdit::WORD_TO_END;
  if (op == "dB" || op == "cB") return BackwardEdit::BIG_WORD_TO_START;
  if (op == "dgE" || op == "cgE") return BackwardEdit::BIG_WORD_TO_END;
  if (op == "d0" || op == "c0" || op == "d^" || op == "c^") return BackwardEdit::LINE_TO_START;
  return nullopt;
}

// Check if operation is valid given boundary constraints
static bool isOpValidForBoundary(const EditState& s, const string& op, const EditBoundary& boundary) {
  // Get current line content and cursor position
  if (s.lines.empty()) return true;  // Empty buffer, any op is fine
  const string& currentLine = s.lines[s.pos.line];
  int cursorCol = s.pos.col;

  // Partial-line region detection
  bool isPartialLineRegion = !boundary.startsAtLineStart || !boundary.endsAtLineEnd;
  bool isMultiLine = s.lines.size() > 1;

  if (isPartialLineRegion && isMultiLine) {
    // Block explicit line join operations - these would merge lines in full buffer
    if (op == "J" || op == "gJ") return false;

    // Block j/k navigation - column offsets differ between lines in partial-line regions.
    // When k goes from line 1 col 0 to line 0 col 0, in full buffer that's a different
    // relative position within each line's edit region (or outside it entirely).
    if (op == "j" || op == "k") return false;
  }

  // Full-line operations (dd, cc, S)
  static const vector<string> FULL_LINE_OPS = {"dd", "cc", "S"};
  bool isFullLineOp = find(FULL_LINE_OPS.begin(), FULL_LINE_OPS.end(), op) != FULL_LINE_OPS.end();

  if (isFullLineOp) {
    // Use existing isFullLineEditSafe
    if (!isFullLineEditSafe(boundary)) {
      return false;  // Would delete content outside edit region
    }

    // dd-specific constraints for cursor escape
    if (op == "dd") {
      bool isLastLine = (s.pos.line == (int)s.lines.size() - 1);
      if (isLastLine && boundary.hasLinesBelow) {
        return false;  // Cursor would escape to content below
      }
      // Can't dd if it would leave us with 0 lines but there are lines above/below
      if (s.lines.size() == 1 && (boundary.hasLinesAbove || boundary.hasLinesBelow)) {
        return false;  // Can't delete the only line if there's surrounding content
      }
    }
    return true;
  }

  // Forward edit operations - check with isForwardEditSafe
  auto fwdType = getForwardEditType(op);
  if (fwdType) {
    bool isLastLine = (s.pos.line == (int)s.lines.size() - 1);
    int lineLen = currentLine.size();
    bool atOrNearLineEnd = (lineLen == 0) || (cursorCol >= lineLen - 1);

    if (*fwdType != ForwardEdit::CHAR && *fwdType != ForwardEdit::LINE_TO_END) {
      // For partial-line multi-line regions, forward word ops from ANY position
      // on non-last lines can reach next line (w/e/E cross lines easily)
      // This would join lines and corrupt the full buffer.
      if (isPartialLineRegion && isMultiLine && !isLastLine) {
        return false;
      }
      // Block at line end if would wrap to next line (join lines)
      if (isMultiLine && atOrNearLineEnd && !isLastLine) {
        return false;
      }
      // On last line with endsAtLineEnd=false: block forward word ops near line end
      // (would escape to content after edit region in full buffer)
      if (isLastLine && !boundary.endsAtLineEnd && atOrNearLineEnd) {
        return false;
      }
    }

    // WORD_TO_START (dw, cw, dW, cW) includes trailing whitespace after the word.
    // On last line with endsAtLineEnd=false, this whitespace is OUTSIDE the edit region.
    // Block these operations entirely on last line for partial-line regions.
    if (*fwdType == ForwardEdit::WORD_TO_START || *fwdType == ForwardEdit::BIG_WORD_TO_START) {
      if (isLastLine && !boundary.endsAtLineEnd) {
        return false;
      }
    }

    // CHAR (x) at last column of last line with endsAtLineEnd=false:
    // After deletion, cursor lands on content OUTSIDE the edit region.
    // Subsequent operations would affect outside content.
    if (*fwdType == ForwardEdit::CHAR) {
      if (isLastLine && !boundary.endsAtLineEnd && atOrNearLineEnd) {
        return false;
      }
    }

    return isForwardEditSafe(currentLine, cursorCol, boundary, *fwdType);
  }

  // Backward edit operations - check with isBackwardEditSafe
  auto bwdType = getBackwardEditType(op);
  if (bwdType) {
    if (*bwdType != BackwardEdit::CHAR && *bwdType != BackwardEdit::LINE_TO_START) {
      // For partial-line multi-line regions, backward word ops from ANY position
      // on non-first lines can reach previous line (ge/b cross lines easily)
      // This would join lines and corrupt the full buffer.
      if (isPartialLineRegion && isMultiLine && s.pos.line > 0) {
        return false;
      }
      // Block at line start if would wrap to previous line (join lines)
      if (isMultiLine && cursorCol == 0 && s.pos.line > 0) {
        return false;
      }
      // On first line with startsAtLineStart=false: block backward word ops from column 0
      // (would escape to content before edit region in full buffer)
      bool isFirstLine = (s.pos.line == 0);
      if (isFirstLine && !boundary.startsAtLineStart && cursorCol == 0) {
        return false;
      }
    }
    return isBackwardEditSafe(currentLine, cursorCol, boundary, *bwdType);
  }

  // Text object operations - need careful boundary checking
  // "inner" (iw, iW) - deletes just the word/WORD
  // "around" (aw, aW) - deletes word/WORD plus surrounding whitespace
  //
  // Key insight from Vim testing:
  // - ciW on "bb" in "bb x" → " x" (keeps space)
  // - caW on "bb" in "bb x" → "x" (deletes space too!)
  //
  // So "around" text objects are DANGEROUS when boundary is at word edge
  // because they'll grab whitespace that's outside the edit region.

  // Inner word text objects - safe when boundary is at word edge (not in middle)
  if (op == "diw" || op == "ciw") {
    return !boundary.left_in_word && !boundary.right_in_word;
  }
  if (op == "diW" || op == "ciW") {
    return !boundary.left_in_WORD && !boundary.right_in_WORD;
  }

  // Around word text objects - only safe when boundary cuts through word
  // (meaning there's no adjacent whitespace to grab outside the region)
  // For partial-line regions, "around" is almost never safe
  if (op == "daw" || op == "caw") {
    // Safe only if BOTH boundaries cut through words (no exposed whitespace)
    // AND we're at full line boundaries (no adjacent content)
    if (!boundary.startsAtLineStart || !boundary.endsAtLineEnd) {
      return false;  // Partial line - around would grab adjacent whitespace
    }
    return boundary.left_in_word && boundary.right_in_word;
  }
  if (op == "daW" || op == "caW") {
    if (!boundary.startsAtLineStart || !boundary.endsAtLineEnd) {
      return false;  // Partial line - around would grab adjacent whitespace
    }
    return boundary.left_in_WORD && boundary.right_in_WORD;
  }

  // 's' (substitute char) is like 'x' then insert
  if (op == "s") {
    return isForwardEditSafe(currentLine, cursorCol, boundary, ForwardEdit::CHAR);
  }

  // Navigation and other operations - generally safe
  return true;
}

// Try to apply an operation to a state, return new state if valid
static optional<EditState> tryApplyOp(const EditState& s, const string& op,
                                       const Config& config, const NavContext& ctx,
                                       const EditBoundary& boundary) {
  // Check boundary constraints before attempting
  if (!isOpValidForBoundary(s, op, boundary)) {
    return nullopt;
  }

  EditState newState = s;
  try {
    Edit::applyEdit(newState.lines, newState.pos, newState.mode, ctx, ParsedEdit(op));
    // Compute cost using ALL_MOTIONS or character-based fallback
    auto it = ALL_MOTIONS.find(op);
    if (it != ALL_MOTIONS.end()) {
      newState.effort.append(it->second, config);
    } else {
      // Try character-based cost for unknown ops
      for (char c : op) {
        auto cit = CHAR_TO_KEYS.find(c);
        if (cit != CHAR_TO_KEYS.end()) {
          newState.effort.append(cit->second, config);
        }
      }
    }
    newState.seq.push_back(op);
    return newState;
  } catch (...) {
    // Operation invalid in this state
    return nullopt;
  }
}

DeletionResult EditOptimizer::optimizeDeletion(const Lines& source, const EditBoundary& boundary) {
  int rows = source.size();
  int maxCols = 0;
  for (const auto& line : source) {
    maxCols = max(maxCols, (int)line.size());
  }
  if (maxCols == 0) maxCols = 1;  // At least 1 column for empty lines

  DeletionResult result(rows, maxCols);
  if (source.empty()) {
    return result;  // Empty source, nothing to do
  }

  debug("DeletionSearch: hasLinesAbove=", boundary.hasLinesAbove,
        "hasLinesBelow=", boundary.hasLinesBelow);

  // NavContext for edit operations
  NavContext ctx(100, 50);  // windowHeight, scrollAmount

  // Priority queue: min-heap by cost
  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;

  // Visited states: track best cost for each (startIndex, buffer, pos, mode) tuple
  // Use map of startIndex -> (stateKey -> cost)
  unordered_map<int, unordered_map<EditStateKey, double, EditStateKeyHash>> visited;

  // Initialize with all starting positions
  for (int r = 0; r < rows; r++) {
    int cols = source[r].empty() ? 1 : source[r].size();
    for (int c = 0; c < cols; c++) {
      EditState initial;
      initial.lines = source;
      initial.pos = Position(r, c);
      initial.mode = Mode::Normal;
      initial.startIndex = r * maxCols + c;
      initial.cost = deletionHeuristic(initial);
      pq.push(initial);
    }
  }

  debug("DeletionSearch: starting with", pq.size(), "positions");

  int expansions = 0;
  const int maxExpansions = 100000;

  while (!pq.empty() && expansions < maxExpansions) {
    EditState current = pq.top();
    pq.pop();

    // Check if already visited with better cost (per startIndex)
    auto key = current.getKey();
    auto& perStartVisited = visited[current.startIndex];
    auto it = perStartVisited.find(key);
    if (it != perStartVisited.end() && it->second <= current.getEffort(config)) {
      continue;
    }
    perStartVisited[key] = current.getEffort(config);
    expansions++;

    // Check if goal
    if (isDeletionGoal(current, boundary)) {
      int idx = current.startIndex;
      if (!result.results[idx].isValid() ||
          current.getEffort(config) < result.results[idx].keyCost) {
        result.results[idx] = Result(current.getSequenceString(), current.getEffort(config));
        debug("Found goal for start", idx, ":", current.getSequenceString(),
              "cost", current.getEffort(config));
      }
      continue;  // Don't expand further from goal
    }

    // Expand: try all operations
    for (const auto& op : DELETION_OPS) {
      auto newState = tryApplyOp(current, op, config, ctx, boundary);
      if (newState) {
        newState->startIndex = current.startIndex;
        newState->cost = newState->getEffort(config) + deletionHeuristic(*newState);

        // Only add if not visited with better cost (per startIndex)
        auto newKey = newState->getKey();
        auto& perStartVis = visited[newState->startIndex];
        auto vit = perStartVis.find(newKey);
        if (vit == perStartVis.end() || vit->second > newState->getEffort(config)) {
          pq.push(*newState);
        }
      }
    }
  }

  debug("DeletionSearch: completed after", expansions, "expansions");

  return result;
}
