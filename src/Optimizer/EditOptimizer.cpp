#include "EditOptimizer.h"

#include "Optimizer/Levenshtein.h"
#include "State/PosKey.h"

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
EditResult
EditOptimizer::optimizeEdit(const Lines &beginLines, const Lines &endLines,
                            // bool canDeleteBackFirstLine,   // d^ at first line?
                            // bool canDeleteForwardLastLine, // D at last line?
                            const MotionToKeys &editMotionToKeys) {
  Levenshtein editDistanceCalculator(endLines.flatten());

  int n = beginLines.size();
  int m = endLines.size();
  // Total sum of lines, not including new line chars
  int x = transform_reduce(beginLines.begin(), beginLines.end(), 0, plus<int>{},
                           [](const string &s) { return s.size(); });
  int y = transform_reduce(endLines.begin(), endLines.end(), 0, plus<int>{},
                           [](const string &s) { return s.size(); });

  EditResult res(x, y);

  int totalExplored = 0;
  double leastCostFound = NOT_FOUND;

  // Debug
  int duplicatedFound = 0;
  vector<string> duplicatedMessages;
  // double userEffort = getEffort(userSequence, config);
  // debug("user effort for sequence", userSequence, "is", userEffort);

  map<PosKey, int> positionToIndex;
  unordered_map<EditStateKey, double, EditStateKeyHash> costMap;

  priority_queue<EditState, vector<EditState>, greater<EditState>> pq;

  function<void(EditState &&)> exploreNewState =
      [this, &pq, &costMap, &endLines](EditState &&newState) {
        if (newState.getEffort() > userEffort * USER_EXPLORE_FACTOR) {
          return;
        }
        // debug("curr:", currentCost, "new:", newCost);
        double newCost = newState.getCost();
        const EditStateKey newKey = newState.getKey();
        auto it = costMap.find(newKey);
        if (it == costMap.end()) {
          // In movement optimizer, we do a check for emplacing, but here we do
          // unconditionally since we only ever want 1 of each state (startIndex
          // encoded in key)
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

  int it = 0;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < beginLines[i].size(); j++) {
      EditState state(beginLines, Position(i, j), Mode::Normal, RunningEffort(),
                      it);
      pq.push(state);
      positionToIndex[PosKey(i, j)] = it++;
      costMap[state.getKey()] = 0;
    }
  }

  while (!pq.empty()) {
    EditState s = pq.top();
    pq.pop();
    Lines lines = s.getLines();
    Position pos = s.getPos();
    Mode mode = s.getMode();
    double cost = s.getCost();
    EditStateKey key = s.getKey();

    if (++totalExplored > MAX_SEARCH_DEPTH) {
      debug("maximum total explored count reached");
      break;
    }
    if(cost > userEffort * USER_EXPLORE_FACTOR) {
      debug("exceeded user explore cost");
      break;
    }
    if(cost > leastCostFound * ABSOLUTE_EXPLORE_FACTOR) {
      debug("exceeded absolute explore cost");
      break;
    }

    bool isDone = (lines == endLines && mode == Mode::Normal);

    if (isDone) {
      int i = s.getStartIndex();
      int j = positionToIndex[PosKey(pos)];
      if(res.adj[i][j].isValid()) {
        duplicatedFound++; 
        duplicatedMessages.push_back("found " + to_string(i) + "->" + to_string(j) + ": " + s.getMotionSequence());
      } else {
        res.adj[i][j] = Result(s.getMotionSequence(), cost);
      }
      // Update leastCostFound
      if (leastCostFound == NOT_FOUND) {
        leastCostFound = cost;
      }
    } else {
      // Prune early if this state is outdated. It is guaranteed to exist in the
      // map
      if (costMap[key] < s.getCost()) {
        continue;
      }
    }

    // TODO: handle count motions. Because contents are very dynamic, and we
    // don't handle many characters anyway, just brute force search until the
    // heuristic declines
    //
    auto exploreMotion = [&](const EditState &base, const string &motion,
                             const KeySequence &keySequence) {
      EditState newState = base;
      newState.applySingleMotion(motion, keySequence);
      newState.updateEffort(keySequence, config);
      newState.updateCost(heuristic(newState, editDistanceCalculator));
      exploreNewState(std::move(newState));
    };

    for (auto [motion, keys] : editMotionToKeys) {
      exploreMotion(s, motion, keys);
    }
  }

  return res;
}
