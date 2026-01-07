#include "MovementOptimizer.h"

#include "BufferIndex.h"
#include "State/PosKey.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"
#include "VimCore/VimMovementUtils.h"

using namespace std;

PhysicalKeys makePhysicalKeys(int count, const PhysicalKeys& motionKeys) {
  PhysicalKeys keys;
  for(char c : to_string(count)) {
    keys.append(CHAR_TO_KEYS.at(c));
  }
  keys.append(motionKeys);
  return keys;
}

vector<Result> MovementOptimizer::optimize(
    const vector<string> &lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position &endPos,
    const string &userSequence,
    const NavContext& navContext,
    const SearchParams& params,
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

  // Create initial state: effort=0 (fresh start), cost=heuristic
  // Only RunningEffort is continued from caller for correct typing effort calculation
  MotionState initialState(startPos, startingEffort, 0.0, 0.0);

  debug("user effort for sequence", userSequence, "is", userEffort);

  vector<Result> res;
  unordered_map<PosKey, double, PosKeyHash> costMap;
  const PosKey goalKey(endPos.line, endPos.col);

  priority_queue<MotionState, vector<MotionState>, greater<MotionState>> pq;

  // Consider making an lvalue overload as well if that is needed anywhere
  // NOTE: Uses <= for cost comparison to allow exploration of equal-cost paths.
  // This ensures we find all optimal sequences (e.g., both 'w' and 'W' when they
  // have equal cost to reach the goal).
  auto exploreNewState = [&](MotionState&& newState) {
    if (newState.getEffort() > userEffort * params.exploreFactor) {
      return;
    }
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
    // Allow equal costs for more exploration - finds all optimal paths
    else if (newCost <= it->second) {
      it->second = newCost;
      pq.push(std::move(newState));
    }
  };

  auto exploreMotionWithKnownKeys = [&](const MotionState& base, const string& motion, const PhysicalKeys& keys) {
    MotionState newState = base;
    newState.applySingleMotion(motion, navContext, lines);
    newState.updateEffort(keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };


  auto exploreMotionCntTimesWithKnownPosition = [&](const MotionState& base, const string& motion, int cnt, const Position& newPos) {
    MotionState newState = base;
    newState.applyMotionWithKnownPosition(motion, cnt, newPos);
    newState.updateEffort(
      makePhysicalKeys(abs(cnt), motionToKeys.at(motion)),
      config
    );
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  auto exploreMotionWithKnownColumnAndKeys = [&](const MotionState& base, const string& motion, int newcol, const PhysicalKeys& keys) {
    MotionState newState = base;
    newState.applySingleMotionWithKnownColumn(motion, newcol);
    newState.updateEffort(keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Start - set cost to heuristic (f = g + h, where g = 0 for fresh start)
  initialState.updateCost(heuristic(initialState, endPos, params.costWeight));
  pq.push(initialState);
  costMap[initialState.getKey()] = initialState.getCost();

  while (!pq.empty()) {
    MotionState s = pq.top();
    pq.pop();
    Position pos = s.getPos();

    if (++totalExplored > params.maxSearchDepth) {
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
      if (res.size() >= static_cast<size_t>(params.maxResults)) {
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
          PhysicalKeys f_keys;
          f_motions += first_motion;               f_keys.append(CHAR_TO_KEYS.at(first_motion));
          f_motions += c;                          f_keys.append(charIt->second);
          f_motions += string(cnt, repeat_motion); f_keys.append(CHAR_TO_KEYS.at(repeat_motion), cnt);

          exploreMotionWithKnownColumnAndKeys(s, f_motions, col, f_keys);
        }
      };

      // F motions
      if(forward){
        handleFMotions(
          VimMovementUtils::generateFMotions<true>(pos.col, endPos.col, lines[pos.line], params.fMotionThreshold),
          'f', ';'
        );
      }
      else {
        handleFMotions(
          VimMovementUtils::generateFMotions<false>(pos.col, endPos.col, lines[pos.line], params.fMotionThreshold),
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
      exploreMotionWithKnownKeys(s, motion, keys);
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

vector<RangeResult> MovementOptimizer::optimizeToRange(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& rangeBegin,
    const Position& rangeEnd,
    const string& userSequence,
    NavContext& navContext,
    const SearchParams& params,
    bool allowMultiplePerPosition,
    const ImpliedExclusions& impliedExclusions,
    const MotionToKeys& rawMotionToKeys) {

  // Apply exclusions
  MotionToKeys motionToKeys = rawMotionToKeys;
  if (impliedExclusions.exclude_G) {
    motionToKeys.erase("G");
  }
  if (impliedExclusions.exclude_gg) {
    motionToKeys.erase("gg");
  }

  int totalExplored = 0;
  double userEffort = getEffort(userSequence, config);

  // Create initial state: effort=0 (fresh start), cost=heuristic
  // Only RunningEffort is continued from caller for correct typing effort calculation
  MotionState initialState(startPos, startingEffort, 0.0, 0.0);

  // Results storage:
  // - allowMultiplePerPosition=false: at most 1 result per position (best cost)
  // - allowMultiplePerPosition=true: all results found per position
  map<PosKey, RangeResult> bestResultByPos;      // Used when !allowMultiplePerPosition
  vector<RangeResult> allResults;                 // Used when allowMultiplePerPosition
  int uniquePositionsFound = 0;

  unordered_map<PosKey, double, PosKeyHash> costMap;

  priority_queue<MotionState, vector<MotionState>, greater<MotionState>> pq;

  // Helper: check if position is in goal range
  auto isInRange = [&](const Position& pos) {
    return pos >= rangeBegin && pos <= rangeEnd;
  };

  // exploreNewState: don't cache goal positions (allow multiple paths)
  // NOTE: Uses <= for cost comparison to allow exploration of equal-cost paths.
  // This ensures we find all optimal sequences (e.g., both 'w' and 'W' when they
  // have equal cost to reach the range).
  auto exploreNewState = [&](MotionState&& newState) {
    if (newState.getEffort() > userEffort * params.exploreFactor) {
      return;
    }
    double newCost = newState.getCost();
    const PosKey newKey = newState.getKey();
    auto it = costMap.find(newKey);
    if (it == costMap.end()) {
      // Don't cache positions in goal range (want multiple results)
      if (!isInRange(newState.getPos())) {
        costMap.emplace(newKey, newCost);
      }
      pq.push(std::move(newState));
    } else if (newCost <= it->second) {
      // Allow equal costs for more exploration - finds all optimal paths
      it->second = newCost;
      pq.push(std::move(newState));
    }
  };

  auto exploreMotion = [&](const MotionState& base, const string& motion, const PhysicalKeys& keys) {
    MotionState newState = base;
    newState.applySingleMotion(motion, navContext, lines);
    newState.updateEffort(keys, config);
    newState.updateCost(heuristicToRange(newState, rangeBegin, rangeEnd, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Start - set cost to heuristic (f = g + h, where g = 0 for fresh start)
  initialState.updateCost(heuristicToRange(initialState, rangeBegin, rangeEnd, params.costWeight));
  pq.push(initialState);
  costMap[initialState.getKey()] = initialState.getCost();

  while (!pq.empty()) {
    MotionState s = pq.top();
    pq.pop();
    Position pos = s.getPos();

    if (++totalExplored > params.maxSearchDepth) {
      debug("optimizeToRange: max search depth reached");
      break;
    }

    PosKey stateKey = s.getKey();
    bool isGoal = isInRange(pos);

    if (isGoal) {
      double effort = s.getRunningEffort().getEffort(config);

      if (allowMultiplePerPosition) {
        // Store all results, no filtering
        allResults.emplace_back(s.getMotionSequence(), effort, pos);
        if (allResults.size() >= static_cast<size_t>(params.maxResults)) {
          debug("optimizeToRange: max results reached");
          break;
        }
      } else {
        // At most 1 result per position, keep best cost
        auto it = bestResultByPos.find(stateKey);
        if (it == bestResultByPos.end()) {
          // New end position
          bestResultByPos.emplace(stateKey, RangeResult(s.getMotionSequence(), effort, pos));
          uniquePositionsFound++;
          if (uniquePositionsFound >= params.maxResults) {
            debug("optimizeToRange: max unique positions reached");
            break;
          }
        } else if (effort < it->second.keyCost) {
          // Strictly better path - replace
          it->second = RangeResult(s.getMotionSequence(), effort, pos);
        }
        // else: same or worse cost, ignore
      }
      continue;
    } else {
      // Prune outdated states
      if (costMap.count(stateKey) && costMap[stateKey] < s.getCost()) {
        continue;
      }
    }

    debug("\"" + s.getMotionSequence() + "\"", s.getCost());

    // Basic motions only (f-motion and count searches disabled for now)
    for (const auto& [motion, keys] : motionToKeys) {
      exploreMotion(s, motion, keys);
    }
  }

  debug("---costMap---");
  map<PosKey, double> tempMap(costMap.begin(), costMap.end()); // Print in order
  for (auto [state, cost] : tempMap) {
    auto [l, c] = state;
    debug(l, c, cost);
  }

  // Return results based on mode
  if (allowMultiplePerPosition) {
    return allResults;
  } else {
    vector<RangeResult> results;
    results.reserve(bestResultByPos.size());
    for (auto& [posKey, result] : bestResultByPos) {
      results.push_back(std::move(result));
    }
    return results;
  }
}
