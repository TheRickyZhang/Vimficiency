#include "ImpliedExclusions.h"
#include "Optimizer.h"
#include "BufferIndex.h"
#include "Editor/NavContext.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"
#include "VimCore/VimUtils.h" // TODO This is probably ugly include, move up?

using namespace std;

ostream &operator<<(ostream &os, const Result &r) {
  os << r.sequence << ", " << r.keyCost << "\n";
  return os;
}

KeySequence makeKeySequence(int count, const KeySequence& motionKeys) {
  KeySequence keys;
  for(char c : to_string(count)) {
    keys.append(CHAR_TO_KEYS.at(c));
  }
  keys.append(motionKeys);
  return keys;
}

vector<Result> Optimizer::optimizeMovement(
    const vector<string> &lines,
    const State& startingState, const Position &endPos,
    const string &userSequence,
    NavContext& navContext,
    const ImpliedExclusions& impliedExclusions,
    const MotionToKeys &rawMotionToKeys) {
  // Apply exclusions. Not sure if copy overhead outweighs skipping later, but it's clear and direct.
  MotionToKeys motionToKeys = rawMotionToKeys;
  if(impliedExclusions.exclude_G) {
    motionToKeys.erase("G");
  }
  if(impliedExclusions.exclude_gg) {
    motionToKeys.erase("gg");
  }

  // Initialize index for faster count searching
  BufferIndex bufferIndex(lines);

  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);

  debug("user effort for sequence", userSequence, "is", userEffort);
  // debug("MAX_RESULT_COUNT:", MAX_RESULT_COUNT);
  // debug("EXPLORE_FACTOR:", EXPLORE_FACTOR);

  vector<Result> res;
  map<PosKey, double> costMap;
  const PosKey goalKey = {endPos.line, endPos.col};

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

  auto exploreMotionWithKnownKeySequence = [&](const State& base, const string& motion, const KeySequence& keySequence) {
    State newState = base;
    newState.applyMotion(motion, 0, navContext, lines);
    newState.effort = newState.effortState.append( keySequence, config);
    newState.cost = heuristic(newState, endPos);
    exploreNewState(std::move(newState));
  };

  auto exploreMotionCntTimes = [&](const State& base, const string& motion, int cnt) {
    State newState = base;
    newState.applyMotion(motion, cnt, navContext, lines);
    newState.effort = newState.effortState.append(
      makeKeySequence(abs(cnt), motionToKeys.at(motion)),
      config
    );
    newState.cost = heuristic(newState, endPos);
    exploreNewState(std::move(newState));
  };

  auto exploreMotionCntTimesWithKnownPosition = [&](const State& base, const string& motion, int cnt, const Position& newPos) {
    State newState = base;
    newState.applyMotionWithKnownPosition(motion, cnt, newPos);
    newState.effort = newState.effortState.append(
      makeKeySequence(abs(cnt), motionToKeys.at(motion)),
      config
    );
    newState.cost = heuristic(newState, endPos);
    exploreNewState(std::move(newState));
  };

  // Start
  pq.push(startingState);
  costMap[startingState.getKey()] = 0;

  // Seed search with exact line jump (e.g., "5j" to go down 5 lines)
  int diff = endPos.line - startingState.pos.line;
  int absDiff = abs(diff);
  if(absDiff >= 3) {
    string motion = diff > 0 ? "j" : "k";
    exploreMotionCntTimes(startingState, motion, abs(diff));
  }

  while (!pq.empty()) {
    State s = pq.top();
    pq.pop();
    Position pos = s.pos;

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

    // double currentCost = heuristic(s, endPos);

    // Process f/F motions with ; when on the same line as end.
    // Note for now we do not consider t or , as they are generally wasteful in comparison. 
    // -------------------- START isSameLine --------------------
    if (isSameLine) {
      // TODO: move handleFMotions out of way if possible
      auto handleFMotions = [&](auto infos, const char first_motion, const char repeat_motion){
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
          newState.setCol(col);
          // Mode unchanged
          newState.motionSequence += f_motions;
          newState.effort = newState.effortState.append(f_key_sequence, config);
          newState.cost = heuristic(newState, endPos);

          exploreNewState(std::move(newState));
        }
      };

      // F Motions
      if(pos.col<endPos.col){
        handleFMotions(
          VimUtils::generateFMotions<true>(pos.col, endPos.col, lines[pos.line], F_MOTION_THRESHOLD),
          'f', ';'
        );
      }
      else {
        handleFMotions(
          VimUtils::generateFMotions<false>(pos.col, endPos.col, lines[pos.line], F_MOTION_THRESHOLD),
          'F', ';'
        );
      }

      for(const auto& motionPair : COUNT_SEARCHABLE_MOTIONS_LINE) {
        string forwardMotion = motionPair.forward;
        string backwardMotion = motionPair.backward;
        LandingType type = motionPair.type; 
        array<RepeatMotionResult, 2> countSearchResults = bufferIndex.getTwoClosest( type, pos, endPos);
        for(const auto& cres : countSearchResults) {
          if(!cres.valid()) continue;
          string motion = endPos > pos ? forwardMotion : backwardMotion;
          exploreMotionCntTimesWithKnownPosition(s, motion, cres.count, cres.pos);
        }
      }
    }
    // -------------------- END isSameLine --------------------

    // -------------------- START global search --------------------
    // By default, motionToKeys is EXPLORABLE_MOTIONS (with exclusions applied)
    for (auto [motion, keys] : motionToKeys) {
      exploreMotionWithKnownKeySequence(s, motion,  keys);
    }

    for(const auto& motionPair : COUNT_SEARCHABLE_MOTIONS_GLOBAL) {
      string forwardMotion = motionPair.forward;
      string backwardMotion = motionPair.backward;
      LandingType type = motionPair.type; 
      array<RepeatMotionResult, 2> results = bufferIndex.getTwoClosest( type, pos, endPos);
      for(const auto& res : results) {
        if(!res.valid()) continue;
        string motion = endPos > pos ? forwardMotion : backwardMotion;
        exploreMotionCntTimesWithKnownPosition(s, motion, res.count, res.pos);
      }
    }
    // -------------------- END global search --------------------
  }

  debug("---costMap---");
  for (auto [state, cost] : costMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }
  return res;
}
