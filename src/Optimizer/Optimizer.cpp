#include "Optimizer.h"

#include "BoundaryFlags.h"
#include "BufferIndex.h"
#include "ImpliedExclusions.h"
#include "EditOptimizer.h"
#include "State/PosKey.h"
#include "Editor/NavContext.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"
#include "VimCore/VimMovementUtils.h" // TODO This is probably ugly include, move up?

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
  unordered_map<PosKey, double, PosKeyHash> costMap;
  const PosKey goalKey(endPos.line, endPos.col);

  priority_queue<State, vector<State>, greater<State>> pq;

  // Consider making an lvalue overload as well if that is needed anywhere
  function<void(State&&)> exploreNewState = [this, &pq, &costMap, &goalKey, &userEffort](State&& newState) {
    if (newState.getEffort() > userEffort * EXPLORE_FACTOR) {
      return;
    }
    // debug("curr:", currentCost, "new:", newCost);
    double newCost = newState.getCost();
    const PosKey newKey = newState.getKey();
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
    newState.applySingleMotion(motion, navContext, lines);
    newState.updateEffort(keySequence, config);
    newState.updateCost(heuristic(newState, endPos));
    exploreNewState(std::move(newState));
  };


  auto exploreMotionCntTimesWithKnownPosition = [&](const State& base, const string& motion, int cnt, const Position& newPos) {
    State newState = base;
    newState.applyMotionWithKnownPosition(motion, cnt, newPos);
    newState.updateEffort(
      makeKeySequence(abs(cnt), motionToKeys.at(motion)),
      config
    );
    newState.updateCost(heuristic(newState, endPos));
    exploreNewState(std::move(newState));
  };

  auto exploreMotionWithKnownColumnAndKeySequence = [&](const State& base, const string& motion, int newcol, const KeySequence& keySequence) {
    State newState = base;
    newState.applySingleMotionWithKnownColumn(motion, newcol);
    newState.updateEffort(keySequence, config);
    newState.updateCost(heuristic(newState, endPos));
    exploreNewState(std::move(newState));
  };

  // Start
  pq.push(startingState);
  costMap[startingState.getKey()] = 0;

  // Seed search with exact line jump (e.g., "5j" to go down 5 lines)
  // int diff = endPos.line - startingState.pos.line;
  // int absDiff = abs(diff);
  // if(absDiff >= 3) {
  //   string motion = diff > 0 ? "j" : "k";
  //   exploreMotionCntTimesWithKnownPosition(startingState, motion, abs(diff), Position(
  //     endPos.line,
  //     min(startingState.pos.targetCol,
  //         static_cast<int>(lines[endPos.line].size()-1)),
  //         startingState.pos.targetCol
  //         ));
  // }

  while (!pq.empty()) {
    State s = pq.top();
    pq.pop();
    Position pos = s.getPos();

    if (++totalExplored > MAX_SEARCH_DEPTH) {
      debug("maximum total explored count reached");
      break;
    }

    PosKey stateKey = s.getKey();
    bool isGoal = (stateKey == goalKey);
    bool isSameLine = (pos.line == endPos.line);
    bool forward = pos < endPos;

    if (isGoal) {
      // TODO: replace with root level call, nothing should expose runningEffort
      res.emplace_back(s.getMotionSequence(), s.getRunningEffort().getEffort(config));
      if (res.size() >= MAX_RESULT_COUNT) {
        debug("maximum result count reached");
        break;
      }
      continue;
    } else {
      // Prune early if this state is outdated. It is guaranteed to exist in the
      // map.
      if (costMap[stateKey] < s.getCost()) {
        continue;
      }
    }

    debug("\"" + s.getMotionSequence() + "\"", s.getCost());

    // double currentCost = heuristic(s, endPos);

    // Process f/F motions with ; when on the same line as end.
    // Note for now we do not consider t or , as they are generally wasteful in comparison. 
    // -------------------- START isSameLine --------------------
    if (isSameLine) {
      // TODO: move handleFMotions out of way if possible
      auto handleFMotions = [&](vector<tuple<char, int, int>> infos, const char first_motion, const char repeat_motion){
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

          exploreMotionWithKnownColumnAndKeySequence(s, f_motions, col, f_key_sequence);
        }
      };

      // F motions
      if(forward){
        handleFMotions(
          VimMovementUtils::generateFMotions<true>(pos.col, endPos.col, lines[pos.line], F_MOTION_THRESHOLD),
          'f', ';'
        );
      }
      else {
        handleFMotions(
          VimMovementUtils::generateFMotions<false>(pos.col, endPos.col, lines[pos.line], F_MOTION_THRESHOLD),
          'F', ';'
        );
      }

      // Count searchable (same line) motions
      for(const auto& motionPair : COUNT_SEARCHABLE_MOTIONS_LINE) {
        string motion = forward ? motionPair.forward : motionPair.backward;
        // Skip if not in map
        if(!motionToKeys.count(motion)) {
          continue;
        }
        LandingType type = motionPair.type;
        array<RepeatMotionResult, 2> countSearchResults = bufferIndex.getTwoClosest( type, pos, endPos);
        for(const auto& cres : countSearchResults) {
          if(!cres.valid()) continue;
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
      string motion = forward ? motionPair.forward : motionPair.backward;
      // Skip if not in allowed set
      if(!motionToKeys.contains(motion)) {
        continue;
      }
      LandingType type = motionPair.type;
      array<RepeatMotionResult, 2> countSearchResults = bufferIndex.getTwoClosest(type, pos, endPos);
      for(const auto& cres : countSearchResults) {
        if(!cres.valid()) continue;
        exploreMotionCntTimesWithKnownPosition(s, motion, cres.count, cres.pos);
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


vector<Result> Optimizer::optimizeChanges(
  const vector<string>& startLines,
  const Position startPos,
  const vector<string>& endLines,
  const Position endPos,
  const NavContext& navigationContext,
  const ImpliedExclusions& impliedExclusions,
  const MotionToKeys& rawMotionToKeys,
  const EditToKeys& editToKeys
) {
  // Ensures proper hashing later, and 10 is buffer in case we insert more text, then delete
  for(const string& s : startLines) { assert(s.size() < MAX_LINE_LENGTH-10); }
  for(const string& s : endLines) { assert(s.size() < MAX_LINE_LENGTH-10); }

  MotionToKeys motionToKeys = rawMotionToKeys;
  if(impliedExclusions.exclude_G) {
    motionToKeys.erase("G");
  }
  if(impliedExclusions.exclude_gg) {
    motionToKeys.erase("gg");
  }

  // DiffSTate {
  //  BoundaryFlags boundaryFlags;
  //  Position changeBegin;
  //  Position changeEnd;
  //  vector<string> startLines;
  //  vector<string> endLines;
  //  
  // }

  // Basically retrieve results from "git diff".
  vector<DiffState> diffStates = Myers::calculate(startLines, endLines);
  int d = diffStates.size();

  // Note that this is 1-shifted, has size d+1
  vector<vector<string>> linesAfterNEdits(d+1);
  linesAfterNEdits[0] = startLines;
  for(int i = 1; i <= d; i++) {
    linesAfterNEdits[i] = apply(diffStates[i-1], linesAfterNEdits[i-1]);
  }

  vector<EditResult> editResults = solveAll(diffStates);

  auto posHash = [this](Position p) {
    return p.col * MAX_LINE_LENGTH + p.line;
  };
  vector<Position> toDiffStateIndex(d * MAX_LINE_LENGTH);
  // Want toDiffStateIndex[hash(pos)] -> tell us which editResult index the current position is at, or -1 if none
  //
  //
  
  
  // TODO:
  return {}; 
}













