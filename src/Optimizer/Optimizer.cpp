#include "Optimizer/Optimizer.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"
#include "VimCore/VimUtils.h" // TODO This is probably ugly include, move up?

using namespace std;

ostream &operator<<(ostream &os, const Result &r) {
  os << r.sequence << ", " << r.keyCost << "\n";
  return os;
}

vector<Result> Optimizer::optimizeMovement(const vector<string> &lines,
                                           const Position &end,
                                           const string &userSequence,
                                           const MotionToKeys &motionToKeys) {
  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);

  debug("user effort for sequence", userSequence, "is", userEffort);
  // debug("MAX_RESULT_COUNT:", MAX_RESULT_COUNT);
  // debug("EXPLORE_FACTOR:", EXPLORE_FACTOR);

  vector<Result> res;
  map<PosKey, double> costMap;
  const PosKey goalKey = {end.line, end.col};

  priority_queue<State, vector<State>, greater<State>> pq;

  // Consider making an lvalue overload as well if that is needed anywhere
  function<void(State&&)> exploreNewState = [this, &pq, &costMap, &goalKey, &userEffort](State&& newState) {
    if (newState.effort > userEffort * EXPLORE_FACTOR) {
      return;
    }
    // debug("curr:", currentCost, "new:", newCost);
    double newCost = newState.cost;
    PosKey newKey = newState.getKey();
    auto it = costMap.find(newKey);
    if (it == costMap.end()) {
      // Because we want multiple results, do not insert a best value for the goal.
      if (newKey != goalKey) {
        costMap.emplace(newKey, newCost);
      }
      pq.push(std::move(newState));
    }
    // Allow for equality for more exploration, mostly in testing.
    else if (newCost <= it->second) {
      it->second = newCost;
      pq.push(std::move(newState));
    }
    // else { debug(motion, "is worse"); }
  };

  // Start
  pq.push(startingState);
  costMap[startingState.getKey()] = 0;

  while (!pq.empty()) {
    State s = pq.top();
    pq.pop();

    if (++totalExplored > MAX_SEARCH_DEPTH) {
      debug("maximum total explored count reached");
      break;
    }

    PosKey stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);
    bool isSameLine = (stateKey.first == goalKey.first);

    if (isGoal) {
      res.emplace_back(s.motionSequence, s.effortState.getEffort(config));
      if (res.size() >= MAX_RESULT_COUNT) {
        debug("maximum result count reached");
        break;
      }
      continue;
    } else {
      // Prune early if this state is outdated. It is guaranteed to exist in the
      // map.
      if (costMap[stateKey] < s.cost) {
        continue;
      }
    }

    debug("\"" + s.motionSequence + "\"", s.cost);

    double currentCost = heuristic(s, end);

    // Process f/F motions with ; when on the same line as end.
    // Note for now we do not consider t or , as they are generally wasteful in comparison. 
    if (isSameLine) {
      auto handle = [&](auto infos, const char first_motion, const char repeat_motion){
        for(const auto& info : infos){
          const auto& [c, col, cnt] = info;

          // Skip characters not in CHAR_TO_KEYS (non-ASCII, emojis, etc.)
          auto charIt = CHAR_TO_KEYS.find(c);
          if (charIt == CHAR_TO_KEYS.end()) {
            debug("skipping unsupported character in f motion:", static_cast<int>(c));
            continue;
          }

          string f_motions;
          KeySequence f_key_sequence;
          f_motions += first_motion;               f_key_sequence.append(CHAR_TO_KEYS.at(first_motion));
          f_motions += c;                          f_key_sequence.append(charIt->second);
          f_motions += string(cnt, repeat_motion); f_key_sequence.append(CHAR_TO_KEYS.at(repeat_motion), cnt);

          State newState = s;
          newState.set_col(col);
          // Mode unchanged
          newState.motionSequence += f_motions;
          newState.effort = newState.effortState.append(f_key_sequence, config);
          newState.cost = heuristic(newState, end);

          exploreNewState(std::move(newState));
        }
      };

      if(s.pos.col<end.col){
        handle(
          VimUtils::generate_f_motions<true>(s.pos.col, end.col, lines[s.pos.line], F_MOTION_THRESHOLD),
          'f', ';'
        );
      }
      else {
        handle(
          VimUtils::generate_f_motions<false>(s.pos.col, end.col, lines[s.pos.line], F_MOTION_THRESHOLD),
          'F', ';'
        );
      }
    }

    for (auto [motion, keys] : motionToKeys) {
      State newState = s;
      newState.apply_normal_motion(motion, lines);
      newState.effort = newState.effortState.append(keys, config);
      newState.cost = heuristic(newState, end);

      exploreNewState(std::move(newState));
    }
  }

  debug("---costMap---");
  for (auto [state, cost] : costMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }
  return res;
}
