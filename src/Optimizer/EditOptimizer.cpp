#include "EditOptimizer.h"

#include "EditBoundary.h"
#include "Keyboard/EditToKeys.h"
#include "Optimizer/Levenshtein.h"
#include "State/PosKey.h"
#include "State/RunningEffort.h"
#include "Utils/Debug.h"

#include <numeric>

using namespace std;

const double NOT_FOUND = numeric_limits<double>::infinity();

// TODO: actually pass in or don't have it
double userEffort = 100.0;

double
EditOptimizer::costToGoal(const Lines &currLines, const Mode &mode,
                          const Levenshtein &editDistanceCalculator) const {
  // Slightly prefer normal mode so that pressing <Esc> is encouraged at the
  // final step
  double modeCost =
      mode == Mode::Normal
          ? 0
          : config.keyInfo[static_cast<size_t>(Key::Key_Esc)].base_cost;
  return modeCost + editDistanceCalculator.distance(currLines.flatten());
}

double
EditOptimizer::heuristic(const EditState &s,
                         const Levenshtein &editDistanceCalculator) const {
  return COST_WEIGHT * s.getEffort() +
         costToGoal(s.getLines(), s.getMode(), editDistanceCalculator);
}

template <typename F = void (*)(int, int, int)>
auto buildPositionIndex(const Lines &lines, F &&onPos = [](int, int, int) {}) {
  map<PosKey, int> res;
  int it = 0;
  for (int i = 0; i < (int)lines.size(); i++) {
    for (int j = 0; j < lines[i].size(); j++) {
      onPos(i, j, it);
      res[PosKey(i, j)] = it++;
    }
  }
  return res;
}

// We only process starting states with targetCol == col
// This is because:
// We already established that the precomputing of edits is optimal/necessary
// Parameterizing on targetCol for exact correctness changes our search space
// from O(c^2) to O(c^4). Even with strong pruning, this is far too large
// ****Most importantly****, edit motions are dominated by horizontal
// movement, all of which reset targetCol anyway, so it doesn't matter (verify
// this).
// TODO: for testing, add checks to see if we ever produce an output sequence
// that is not correct with the result starting from a different targetCol
EditResult EditOptimizer::optimizeEdit(const Lines &beginLines,
                                       const Lines &endLines,
                                       const EditBoundary& boundary) {
  Levenshtein editDistanceCalculator(endLines.flatten());

  int n = beginLines.size();
  int m = endLines.size();
  // Total sum of lines, not including new line chars
  int x = transform_reduce(beginLines.begin(), beginLines.end(), 0, plus<int>{},
                           [](const string &s) { return s.size(); });
  int y = transform_reduce(endLines.begin(), endLines.end(), 0, plus<int>{},
                           [](const string &s) { return s.size(); });

  debug("=== EditOptimizer::optimizeEdit ===");
  debug("beginLines:", n, "lines,", x, "chars");
  for (size_t i = 0; i < beginLines.size(); i++) {
    debug("  [" + to_string(i) + "] \"" + beginLines[i] + "\"");
  }
  debug("endLines:", m, "lines,", y, "chars");
  for (size_t i = 0; i < endLines.size(); i++) {
    debug("  [" + to_string(i) + "] \"" + endLines[i] + "\"");
  }
  debug("Result matrix size:", x, "x", y);

  EditResult res(x, y);

  int totalExplored = 0;
  double leastCostFound = NOT_FOUND;

  // Debug
  int duplicatedFound = 0;
  vector<string> duplicatedMessages;
  // double userEffort = getEffort(userSequence, config);
  // debug("user effort for sequence", userSequence, "is", userEffort);

  unordered_map<EditStateKey, double, EditStateKeyHash> costMap;

  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;

  // Build position-to-index maps for begin and end lines
  // Create single SharedLines to share among all initial states
  SharedLines sharedBeginLines = std::make_shared<const Lines>(beginLines);

  map<PosKey, int> beginPositionToIndex =
      buildPositionIndex(beginLines, [&](int i, int j, int it) {
        EditState state(sharedBeginLines, Position(i, j), Mode::Normal,
                        RunningEffort(), it);
        pq.push(state);
        costMap[state.getKey()] = 0;
      });
  map<PosKey, int> endPositionToIndex = buildPositionIndex(endLines);

  function<void(EditState &&)> exploreNewState = [this, &pq, &costMap, &endLines](EditState &&newState) {
    if (newState.getEffort() > userEffort * USER_EXPLORE_FACTOR) {
      return;
    }
    // debug("curr:", currentCost, "new:", newCost);
    double newCost = newState.getCost();
    const EditStateKey newKey = newState.getKey();
    auto it = costMap.find(newKey);
    if (it == costMap.end()) {
      // In movement optimizer, we do a check for emplacing, but here we do
      // unconditionally since we only ever want 1 of each state (startIndex encoded in key)
      costMap.emplace(newKey, newCost);
      pq.push(std::move(newState));
    }
    // Allow for equality for more exploration, mostly in testing.
    else if (newCost <= it->second) {
      it->second = newCost;
      pq.push(std::move(newState));
    }
    // else { debug(motion, "is worse"); }
  };

  while (!pq.empty()) {
    EditState s = pq.top();
    pq.pop();
    const Lines &lines = s.getLines();
    int n = lines.size();
    Position pos = s.getPos();
    Mode mode = s.getMode();
    double cost = s.getCost();
    EditStateKey key = s.getKey();

    if (++totalExplored > MAX_SEARCH_DEPTH) {
      debug("maximum total explored count reached");
      break;
    }
    if (cost > userEffort * USER_EXPLORE_FACTOR) {
      debug("exceeded user explore cost");
      break;
    }
    if (cost > leastCostFound * ABSOLUTE_EXPLORE_FACTOR) {
      debug("exceeded absolute explore cost");
      break;
    }

    bool isDone = (lines == endLines && mode == Mode::Normal);

    if (isDone) {
      int i = s.getStartIndex();
      auto it = endPositionToIndex.find(PosKey(pos));
      if (it == endPositionToIndex.end()) continue;  // Position not in end buffer
      int j = it->second;
      if (j < 0 || j >= res.m) continue;  // Bounds check
      if (res.adj[i][j].isValid()) {
        duplicatedFound++;
        duplicatedMessages.push_back("found " + to_string(i) + "->" +
                                     to_string(j) + ": " +
                                     s.getMotionSequence());
      } else {
        res.adj[i][j] = Result(s.getMotionSequence(), cost);
        debug("Result [" + to_string(i) + "][" + to_string(j) + "] = \"" +
              s.getMotionSequence() + "\" cost=" + to_string(cost));
      }
      // Thes first leastCostFound should be minimal, but guard just in case.
      if(leastCostFound != NOT_FOUND && cost < leastCostFound) {
        debug("leastCostFound was", leastCostFound, "but found", cost);
      }
      leastCostFound = min(leastCostFound, cost);
    } else {
      // Prune early if state is outdated. It is guaranteed to exist in the map
      if (costMap[key] < s.getCost()) {
        continue;
      }
    }

    // TODO: handle count motions. Because contents are very dynamic, and we
    // don't handle many characters anyway, just brute force search until the
    // heuristic declines


    auto exploreMotion = [&](const EditState &base, const string &motion,
                             const KeySequence &keySequence) {
      EditState newState = base;
      newState.applySingleMotion(motion, keySequence);
      newState.updateEffort(keySequence, config);
      newState.updateCost(heuristic(newState, editDistanceCalculator));
      exploreNewState(std::move(newState));
    };

    // Helper: explore all motions in a category if condition is true
    auto explore = [&](bool condition, const auto& motions) {
      if (condition) {
        for (auto [motion, keys] : motions) {
          exploreMotion(s, motion, keys);
        }
      }
    };

    namespace N = EditCategory::Normal;
    namespace I = EditCategory::Insert;

    // Per-position safety checking
    const string& line = lines[pos.line];
    int col = pos.col;
    bool lineNonEmpty = !line.empty();

    // Shorthand for boundary checks (already false if line empty via col checks)
    auto fwdSafe = [&](ForwardEdit e) { return lineNonEmpty && isForwardEditSafe(line, col, boundary, e); };
    auto bwdSafe = [&](BackwardEdit e) { return lineNonEmpty && isBackwardEditSafe(line, col, boundary, e); };

    // Position checks
    bool canGoUp = pos.line > 0;
    bool canGoDown = pos.line < n - 1;
    bool canGoLeft = pos.line > 0 || pos.col > 0;
    bool canGoRight = pos.line < n - 1 || pos.col < (int)lines.back().size();

    // Additional checks for motions that would be no-ops
    bool notAtBufferStart = !(pos.line == 0 && pos.col == 0);
    bool notAtBufferEnd = pos.line < n - 1 || (lineNonEmpty && pos.col < (int)line.size() - 1);
    bool colNotZero = col > 0;
    bool colNotAtEnd = lineNonEmpty && pos.col < (int)line.size() - 1;  // Normal mode: last char
    bool colNotAtEndInsert = pos.col < (int)line.size();  // Insert mode: can be at line.size()

    if (mode == Mode::Insert) {
      // Split CHAR_LEFT/CHAR_RIGHT into individual commands with precise conditions
      // <Left>: needs col > 0 (no-op otherwise)
      // <BS>: needs col > 0 || line > 0 (can join with previous line)
      // <Esc>: always works
      // <Right>: needs col < line.size() (no-op otherwise)
      // <Del>: needs col < line.size() || line < n-1 (can join with next line)

      if (colNotZero) exploreMotion(s, "<Left>", {Key::Key_Left});
      if (canGoLeft) exploreMotion(s, "<BS>", {Key::Key_Backspace});
      exploreMotion(s, "<Esc>", {Key::Key_Esc});

      if (colNotAtEndInsert) exploreMotion(s, "<Right>", {Key::Key_Right});
      bool canDelForward = colNotAtEndInsert || pos.line < n - 1;
      if (canDelForward) exploreMotion(s, "<Del>", {Key::Key_Delete});

      explore(colNotZero && bwdSafe(BackwardEdit::WORD_TO_START), I::WORD_LEFT);   // <C-w>
      explore(colNotZero && bwdSafe(BackwardEdit::LINE_TO_START), I::LINE_LEFT);   // <C-u>

      explore(canGoUp, I::LINE_UP);     // <Up>
      explore(canGoDown, I::LINE_DOWN); // <Down>
    }
    else if (mode == Mode::Normal) {
      explore(bwdSafe(BackwardEdit::CHAR), N::CHAR_LEFT);            // X (already checks col > 0)
      explore(fwdSafe(ForwardEdit::CHAR), N::CHAR_RIGHT);            // x

      explore(notAtBufferStart && bwdSafe(BackwardEdit::WORD_TO_START), N::WORD_LEFT);           // db
      explore(notAtBufferStart && bwdSafe(BackwardEdit::WORD_TO_END), N::WORD_END_LEFT);         // dge
      explore(notAtBufferStart && bwdSafe(BackwardEdit::BIG_WORD_TO_START), N::BIG_WORD_LEFT);   // dB
      explore(notAtBufferStart && bwdSafe(BackwardEdit::BIG_WORD_TO_END), N::BIG_WORD_END_LEFT); // dgE
      explore(colNotZero && bwdSafe(BackwardEdit::LINE_TO_START), N::LINE_LEFT);                 // d0

      explore(notAtBufferEnd && fwdSafe(ForwardEdit::WORD_TO_START), N::WORD_RIGHT);           // dw
      explore(notAtBufferEnd && fwdSafe(ForwardEdit::WORD_TO_END), N::WORD_END_RIGHT);         // de
      explore(notAtBufferEnd && fwdSafe(ForwardEdit::BIG_WORD_TO_START), N::BIG_WORD_RIGHT);   // dW
      explore(notAtBufferEnd && fwdSafe(ForwardEdit::BIG_WORD_TO_END), N::BIG_WORD_END_RIGHT); // dE
      explore(colNotAtEnd && fwdSafe(ForwardEdit::LINE_TO_END), N::LINE_RIGHT);               // D, d$

      explore(isFullLineEditSafe(boundary), N::FULL_LINE);           // dd, cc

      // Line navigation (valid even on empty lines)
      explore(canGoUp, N::LINE_UP);
      explore(canGoDown, N::LINE_DOWN);
    }
  }

  // Summary
  int validResults = 0;
  for (int i = 0; i < res.n; i++) {
    for (int j = 0; j < res.m; j++) {
      if (res.adj[i][j].isValid()) validResults++;
    }
  }
  debug("=== EditOptimizer Summary ===");
  debug("Total explored:", totalExplored);
  debug("Valid results:", validResults, "/", res.n * res.m);
  debug("Duplicates found:", duplicatedFound);
  if (leastCostFound != NOT_FOUND) {
    debug("Least cost found:", leastCostFound);
  }

  return res;
}
