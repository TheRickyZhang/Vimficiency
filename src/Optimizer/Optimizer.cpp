#include "Optimizer/Optimizer.h"
#include "Keyboard/CharacterToKeys.h"

using namespace std;

vector<Result> Optimizer::optimizeMovement(const vector<string>& lines, const Position& end, const string& userSequence) {
  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);
  cout << "user effort for sequence " << userSequence << " is " << userEffort << endl;  

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
      cout<<"maximum total explored count reached"<<endl;
      break;
    }

    PosKey stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);

    if(isGoal) {
      res.emplace_back(s.sequence, s.effortState.getEffort(config));
      if(res.size() >= RESULT_COUNT) {
        cout<<"maximum result count reached"<<endl;
        break;
      }
      continue;
    } else { 
      // Prune early if this state is outdated. It is guaranteed to exist in the map.
      if(costMap[stateKey] < s.cost) {
        continue;
      }
    }

    cout << "\"" << s.sequence << "\" " << s.cost << endl;

    double currentCost = heuristic(s, end);
    for(auto [motion, keys] : motionToKeys) {
      State newState = s;
      newState.effort = newState.effortState.append(keys, config);
      newState.apply_normal_motion(motion, lines);
      double newCost = heuristic(newState, end); 
      newState.cost = newCost;
      
      if(newState.effort > userEffort) {
        continue;
      }
      // cout << "curr: " << currentCost << " new: " << newCost << endl;

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
      else {
        // cout << motion << " is worse" << endl;
      }
    }
  }

  cout << "---costMap---" << endl;
  for(auto [state, cost] : costMap) {
    auto [l, c] = state;
    cout << l << " " << c << ", " << cost << endl;
  }
  return res;
}
