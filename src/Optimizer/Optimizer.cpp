#include "Optimizer/Optimizer.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"

using namespace std;

ostream& operator<<(ostream& os, const Result& r) {
  os << r.sequence << ", " << r.keyCost << "\n";
  return os;
}


vector<Result> Optimizer::optimizeMovement(
  const vector<string>& lines, const Position& end, const string& userSequence,
  const MotionToKeys& motionToKeys
) {
  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);

  debug("user effort for sequence", userSequence, "is", userEffort);  
  // debug("MAX_RESULT_COUNT:", MAX_RESULT_COUNT);
  // debug("EXPLORE_FACTOR:", EXPLORE_FACTOR);

  vector<Result> res;
  map<PosKey, double> costMap;
  const PosKey goalKey = {end.line, end.col};

  priority_queue<State, vector<State>, greater<State>> pq;
  
  pq.push(startingState);
  costMap[startingState.getKey()] = 0;

  while(!pq.empty()) {
    State s = pq.top();
    pq.pop();

    if(++totalExplored > MAX_SEARCH_DEPTH) {
      debug("maximum total explored count reached");
      break;
    }

    PosKey stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);

    if(isGoal) {
      res.emplace_back(s.sequence, s.effortState.getEffort(config));
      if(res.size() >= MAX_RESULT_COUNT) {
        debug("maximum result count reached");
        break;
      }
      continue;
    } else { 
      // Prune early if this state is outdated. It is guaranteed to exist in the map.
      if(costMap[stateKey] < s.cost) {
        continue;
      }
    }

    debug("\"" + s.sequence + "\"", s.cost);

    double currentCost = heuristic(s, end);
    for(auto [motion, keys] : motionToKeys) {
      State newState = s;
      newState.effort = newState.effortState.append(keys, config);
      newState.apply_normal_motion(motion, lines);
      double newCost = heuristic(newState, end); 
      newState.cost = newCost;

      if(newState.effort > userEffort * EXPLORE_FACTOR) {
        continue;
      }
      // debug("curr:", currentCost, "new:", newCost);

      PosKey newKey = newState.getKey();
      auto it = costMap.find(newKey);
      if(it == costMap.end()) {
        // Because we want multiple results, do not insert a best value for the goal.
        if(newKey != goalKey) {
          costMap.emplace(newKey, newCost);
        }
        pq.push(newState);
      }
      // Allow for equality for more flexibility. With real double values, this is unlikely to matter.
      else if(newCost <= it->second) {
        it->second = newCost;
        pq.push(newState);
      }
      // else {
        // debug(motion, "is worse");
      // }
    }
  }

  debug("---costMap---");
  for(auto [state, cost] : costMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }
  return res;
}
