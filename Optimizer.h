#include <bits/stdc++.h>
using namespace std;
#include "EffortState.h"
#include "EffortModel.h"
#include "CharacterToKeys.cpp"

using PosKey = pair<int, int>;

struct position {
  int line;
  int currChar;
  int maxChar; // For accurately tracking line movement 

  position(int l, int cc, int mc) {
    line = l;
    cc = cc;
    maxChar = mc;
  }

  position getNewPosition(string& motion, )
};


int costToGoal(position p, position q) {
  return abs(q.line - p.line) + abs(q.currChar - p.currChar);
}

struct Optimizer {
  EffortState baseState;
  EffortModel baseModel; 
  static constexpr int RESULT_COUNT = 5;
  static constexpr int MAX_SEARCH_DEPTH = 1e5;
  double costWeight = 1;

  Optimizer(const EffortState& effortState, const EffortModel& effortModel, int costWeight = 1) 
  : baseState(std::move(effortState)), baseModel(std::move(effortModel)), costWeight(costWeight)
  {
  }

  struct State {
    position pos;
    PosKey key;
    int cost; // Same as state.get_cost()
    EffortState state;

    State(position pos, int cost, EffortState state)
      : pos(pos), cost(cost), state(state) {
      key = {pos.line, pos.currChar};
    }

    bool operator<(const State& other) {
      return cost < other.cost;
    }
  };

    
  vector<string> optimizeMovement(const vector<string>& buffer, position start, position end) {
    map<PosKey, int> costMap;

    auto totalCost = [&](State s) {
      return costWeight * s.cost + costToGoal(s.pos, end);
    }
    
    priority_queue<State, vector<State>, typedef(overallCost)> pq;
    
    State startState = State(start, 0, baseState);
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
          EffortState newState = baseState;
          int newCost = newState.append(keys, baseModel);
          if(newCost )
      }
    }
  }
  
};
