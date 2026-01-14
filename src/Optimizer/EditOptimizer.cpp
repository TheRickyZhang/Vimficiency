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
#include "Keyboard/MotionToKeys.h"
#include "State/EditState.h"
#include "State/RunningEffort.h"
#include "Utils/Debug.h"

#include <queue>
#include <unordered_map>
#include <optional>

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
  bool isPartialLineRegion = !boundary.atLineStart() || !boundary.atLineEnd();
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

// =============================================================================
// Boundary constraint checking
// =============================================================================
//
// Uses endpoint-based crossing functions to determine if operations are safe.
// See EditBoundaryLogic.typ for the crossing tables.

// Check if a forward word motion is valid (not blocked by position constraints)
static bool isForwardWordPositionValid(
    const EditState& s, const EditBoundary& boundary, bool isMultiLine, bool isPartialLineRegion) {
  bool isLastLine = (s.pos.line == (int)s.lines.size() - 1);
  const string& currentLine = s.lines[s.pos.line];
  int lineLen = currentLine.size();
  bool atOrNearLineEnd = (lineLen == 0) || (s.pos.col >= lineLen - 1);

  // For partial-line multi-line regions, forward word ops from ANY position
  // on non-last lines can reach next line (w/e/E cross lines easily)
  if (isPartialLineRegion && isMultiLine && !isLastLine) {
    return false;
  }
  // Block at line end if would wrap to next line (join lines)
  if (isMultiLine && atOrNearLineEnd && !isLastLine) {
    return false;
  }
  // On last line with atLineEnd=false: block forward word ops near line end
  if (isLastLine && !boundary.atLineEnd() && atOrNearLineEnd) {
    return false;
  }
  return true;
}

// Check if a backward word motion is valid (not blocked by position constraints)
static bool isBackwardWordPositionValid(
    const EditState& s, const EditBoundary& boundary, bool isMultiLine, bool isPartialLineRegion) {
  bool isFirstLine = (s.pos.line == 0);
  int cursorCol = s.pos.col;

  // For partial-line multi-line regions, backward word ops from ANY position
  // on non-first lines can reach previous line
  if (isPartialLineRegion && isMultiLine && s.pos.line > 0) {
    return false;
  }
  // Block at line start if would wrap to previous line (join lines)
  if (isMultiLine && cursorCol == 0 && s.pos.line > 0) {
    return false;
  }
  // On first line with atLineStart=false: block backward word ops from column 0
  if (isFirstLine && !boundary.atLineStart() && cursorCol == 0) {
    return false;
  }
  return true;
}

// Check if operation is valid given boundary constraints
static bool isOpValidForBoundary(const EditState& s, const string& op, const EditBoundary& boundary) {
  if (s.lines.empty()) return true;
  const string& currentLine = s.lines[s.pos.line];
  int cursorCol = s.pos.col;

  // Compute edge characters for crossing checks (conservative: use content edges)
  CharType lastChar = currentLine.empty() ? CharType::Newline : getCharType(currentLine.back());
  CharType firstChar = currentLine.empty() ? CharType::Newline : getCharType(currentLine.front());

  bool isPartialLineRegion = !boundary.atLineStart() || !boundary.atLineEnd();
  bool isMultiLine = s.lines.size() > 1;
  bool isLastLine = (s.pos.line == (int)s.lines.size() - 1);
  bool isFirstLine = (s.pos.line == 0);
  int lineLen = currentLine.size();
  bool atOrNearLineEnd = (lineLen == 0) || (cursorCol >= lineLen - 1);

  // Block line join operations in partial-line multi-line regions
  if (isPartialLineRegion && isMultiLine) {
    if (op == "J" || op == "gJ" || op == "j" || op == "k") return false;
  }

  // Full-line operations (dd, cc, S)
  if (op == "dd" || op == "cc" || op == "S") {
    if (!isFullLineEditSafe(boundary)) return false;
    if (op == "dd") {
      if (isLastLine && boundary.hasLinesBelow) return false;
      if (s.lines.size() == 1 && (boundary.hasLinesAbove || boundary.hasLinesBelow)) return false;
    }
    return true;
  }

  // Forward word motions: dw/cw (Space endpoint)
  if (op == "dw" || op == "cw") {
    if (!isForwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    // Space motions include trailing whitespace - block on partial line end
    if (isLastLine && !boundary.atLineEnd()) return false;
    return !canSpaceCross(lastChar, boundary.rightBoundaryChar);
  }

  // Forward word-end motions: de/ce (End endpoint)
  if (op == "de" || op == "ce") {
    if (!isForwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    return !canEndCross(lastChar, boundary.rightBoundaryChar);
  }

  // Forward WORD motions: dW/cW (SPACE endpoint)
  if (op == "dW" || op == "cW") {
    if (!isForwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    if (isLastLine && !boundary.atLineEnd()) return false;
    return !canSpaceCrossWORD(lastChar, boundary.rightBoundaryChar);
  }

  // Forward WORD-end motions: dE/cE (END endpoint)
  if (op == "dE" || op == "cE") {
    if (!isForwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    return !canEndCrossWORD(lastChar, boundary.rightBoundaryChar);
  }

  // Line-to-end motions: D/C/d$/c$ (Line endpoint)
  if (op == "D" || op == "C" || op == "d$" || op == "c$") {
    return !canLineCross(boundary.rightBoundaryChar);
  }

  // Backward word motions: db/cb (End endpoint - symmetric with de)
  if (op == "db" || op == "cb") {
    if (!isBackwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    return !canEndCross(firstChar, boundary.leftBoundaryChar);
  }

  // Backward word-end motions: dge/cge (Next endpoint)
  if (op == "dge" || op == "cge") {
    if (!isBackwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    return !canNextCross(firstChar, boundary.leftBoundaryChar);
  }

  // Backward WORD motions: dB/cB (END endpoint)
  if (op == "dB" || op == "cB") {
    if (!isBackwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    return !canEndCrossWORD(firstChar, boundary.leftBoundaryChar);
  }

  // Backward WORD-end motions: dgE/cgE (NEXT endpoint)
  if (op == "dgE" || op == "cgE") {
    if (!isBackwardWordPositionValid(s, boundary, isMultiLine, isPartialLineRegion)) return false;
    return !canNextCrossWORD(firstChar, boundary.leftBoundaryChar);
  }

  // Line-to-start motions: d0/c0/d^/c^ (Line endpoint)
  if (op == "d0" || op == "c0" || op == "d^" || op == "c^") {
    return !canLineCross(boundary.leftBoundaryChar);
  }

  // Single char forward: x, s (position-based check)
  if (op == "x" || op == "s") {
    if (currentLine.empty()) return false;
    if (cursorCol < 0 || cursorCol >= lineLen) return false;
    // At last column of partial-line region: cursor would land outside
    if (isLastLine && !boundary.atLineEnd() && atOrNearLineEnd) return false;
    return true;
  }

  // Single char backward: X (position-based check)
  if (op == "X") {
    return cursorCol > 0;
  }

  // Text objects: only safe at full line boundaries for now
  // TODO: Implement proper crossing checks for text objects
  if (op == "diw" || op == "ciw" || op == "diW" || op == "ciW" ||
      op == "daw" || op == "caw" || op == "daW" || op == "caW") {
    return boundary.atLineStart() && boundary.atLineEnd();
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
