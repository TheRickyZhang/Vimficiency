#include <bits/stdc++.h>
using namespace std;
#include "EffortState.h"
#include "Config.h"
#include "State.h"
#include "CharacterToKeys.cpp"


int costToGoal(Position p, Position q) {
  return abs(q.line - p.line) + abs(q.targetCol - p.targetCol);
}

struct Optimizer {
  Config config; 
  State state;

  static constexpr int RESULT_COUNT = 5;
  static constexpr int MAX_SEARCH_DEPTH = 1e5;
  double costWeight = 1;

  Optimizer(const State& state, const Config& effortModel, int costWeight = 1) 
  : state(std::move(state)), config(std::move(effortModel)), costWeight(costWeight)
  {
  }

  vector<string> optimizeMovement(const vector<string>& lines, Position start, Position end) {
    map<PosKey, int> costMap;

    auto totalCost = [&](const State& s) {
      return costWeight * s.cost + costToGoal(s.pos, end);
    };
    
    priority_queue<State> pq;
    // priority_queue<State, vector<State>, typedef(overallCost)> pq;
    
    State startState = State(start, 0, state.effort);
    pq.push(startState);
    costMap[startState.key] = 0;

    while(!pq.empty()) {
      State s = pq.top();
      pq.pop();
      
      // Outdated
      if(costMap[s.key] < s.cost) {
        continue;
      }
      for(auto [motion, keys] : motionToKeys) {
          State newState = s;
          int newKeyCost = newState.effort.append(keys, config);
          int newDistCost = apply_normal_motion(newState, motion, lines);
          int goalCost = 

          if(newCost +)
      }
    }
  }
  
};
