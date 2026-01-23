#include "MotionOptimizer.h"

#include "BufferIndex.h"
#include "MotionToSpec.h"
#include "State/PosKey.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "Utils/Debug.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

vector<Result> MotionOptimizer::optimize(
    const Lines &lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position &endPos,
    const string &userSequence,
    const NavContext& navContext,
    const MotionBoundary& boundary,
    const MotionToKeys &rawMotionToKeys,
    const optional<OptimizerParams>& paramsOverride) {
  // Merge defaults with overrides
  const OptimizerParams params = OptimizerParams::merge(defaultParams, paramsOverride);
  // rawMotionToKeys only used for count searchable motion key lookups
  // Spec-based exploration automatically includes all supported motions

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

  // Boundary parameters - follow EditSearchContext pattern:
  // - Column offsets for word motions (passed to motionWordEndpoint)
  // - hasLinesAbove/Below for line-crossing motions (paragraph/sentence endpoint checks)
  bool isBounded = boundary.hasLinesAbove() || boundary.hasLinesBelow();

  // ==========================================================================
  // Exploration helpers with endpoint-first boundary checking
  // ==========================================================================

  // Simple line motions (h, l, 0, ^, $) - no boundary checking needed
  auto exploreSimpleLineMotion = [&](const MotionState& base, const Motion::SimpleLineMotionSpec& spec) {
    MotionState newState = base;
    newState.applySingleMotion(spec.cmd, navContext, lines);
    newState.updateEffort(spec.keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Vertical motions (j, k) - check line boundaries
  auto exploreVerticalMotion = [&](const MotionState& base, const Motion::VerticalMotionSpec& spec) {
    Position pos = base.getPos();
    int lastLine = static_cast<int>(lines.size()) - 1;

    // Compute endpoint
    int targetLine = spec.isDown ? pos.line + 1 : pos.line - 1;

    // Check line boundaries BEFORE applying motion
    if (spec.isDown && boundary.hasLinesBelow() && targetLine > lastLine) {
      return;  // Would escape below
    }
    if (!spec.isDown && boundary.hasLinesAbove() && targetLine < 0) {
      return;  // Would escape above
    }

    // Motion is safe - apply and explore
    MotionState newState = base;
    newState.applySingleMotion(spec.cmd, navContext, lines);
    newState.updateEffort(spec.keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Word motions - use motionWordEndpoint for boundary checking
  // Follows EditSearchContext pattern: pass column offset and hasLinesOutside directly
  auto exploreWordMotion = [&](const MotionState& base, const Motion::WordMotionSpec& spec) {
    Position pos = base.getPos();

    // Compute endpoint FIRST with boundary checking
    // Forward: protect suffix (rightColOffset cols at end of last line)
    // Backward: protect prefix (leftColOffset cols at start of line 0)
    int boundaryOffset = spec.forward ? boundary.rightColOffset() : boundary.leftColOffset();
    bool hasLinesOutside = spec.forward ? boundary.hasLinesBelow() : boundary.hasLinesAbove();

    Position endpoint = VimCore::motionWordEndpoint(
        pos, lines, spec.forward, spec.edgeType, spec.big, spec.skipCurrent,
        boundaryOffset, hasLinesOutside);

    // Check if motion would escape bounds
    if (endpoint == POSITION_OUTSIDE_BOUNDARY) {
      return;  // Motion would escape - skip
    }

    // Motion is safe - apply and explore
    MotionState newState = base;
    newState.applySingleMotion(spec.cmd, navContext, lines);
    newState.updateEffort(spec.keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Paragraph motions ({, }) - use motionParagraphEndpoint for boundary checking
  auto exploreParagraphMotion = [&](const MotionState& base, const Motion::ParagraphMotionSpec& spec) {
    Position pos = base.getPos();
    int lastLine = static_cast<int>(lines.size()) - 1;

    // Compute boundary line for endpoint function
    // Forward: if hasLinesBelow, can't trust landing on last line (may have been clamped)
    // Backward: if hasLinesAbove, can't trust landing on first line
    int boundaryLine = -1;  // -1 means no boundary check
    if (spec.forward && boundary.hasLinesBelow()) {
      boundaryLine = lastLine;  // Flag if endpoint >= lastLine
    } else if (!spec.forward && boundary.hasLinesAbove()) {
      boundaryLine = 0;  // Flag if endpoint <= 0
    }

    int endpointLine = VimCore::motionParagraphEndpoint(
        pos.line, lines, spec.forward, LineEdgeType::NextEdge, boundaryLine);

    // -1 means motion would escape bounds
    if (endpointLine == -1) {
      return;
    }

    // Motion is safe - apply and explore
    MotionState newState = base;
    newState.applySingleMotion(spec.cmd, navContext, lines);
    newState.updateEffort(spec.keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Sentence motions ((, )) - use motionSentenceEndpoint for boundary checking
  auto exploreSentenceMotion = [&](const MotionState& base, const Motion::SentenceMotionSpec& spec) {
    Position pos = base.getPos();
    int lastLine = static_cast<int>(lines.size()) - 1;

    // Compute boundary position for endpoint function
    // Forward: if hasLinesBelow, can't trust landing on last line
    // Backward: if hasLinesAbove, can't trust landing on first line
    Position boundaryPos = POSITION_OUTSIDE_BOUNDARY;  // Invalid means no boundary check
    if (spec.forward && boundary.hasLinesBelow()) {
      // Any position on last line is suspicious
      boundaryPos = Position(lastLine, 0);
    } else if (!spec.forward && boundary.hasLinesAbove()) {
      // Any position on first line is suspicious
      boundaryPos = Position(0, INT_MAX);
    }

    Position endpoint = VimCore::motionSentenceEndpoint(
        pos, lines, spec.forward, SentenceEdgeType::NextEdge, boundaryPos);

    // POSITION_OUTSIDE_BOUNDARY means motion would escape bounds
    if (endpoint == POSITION_OUTSIDE_BOUNDARY) {
      return;
    }

    // Motion is safe - apply and explore
    MotionState newState = base;
    newState.applySingleMotion(spec.cmd, navContext, lines);
    newState.updateEffort(spec.keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Scroll motions (<C-d>, <C-u>) - check line boundaries
  // NOTE: <C-f>/<C-b> excluded due to viewport state dependency (see MotionToSpec.cpp)
  auto exploreScrollMotion = [&](const MotionState& base, const Motion::ScrollMotionSpec& spec) {
    Position pos = base.getPos();

    // Compute line shift based on spec
    int shift;
    if (spec.isHalf) {
      shift = spec.shiftMultiplier * navContext.scrollAmount;
    } else {
      shift = spec.shiftMultiplier * max(0, navContext.windowHeight - 2);
    }

    int targetLine = VimCore::scrollEndpoint(
        pos.line, static_cast<int>(lines.size()), shift,
        boundary.hasLinesAbove(), boundary.hasLinesBelow());

    if (targetLine != VimCore::LINE_OUTSIDE_BOUNDARY) {
      MotionState newState = base;
      newState.applySingleMotion(spec.cmd, navContext, lines);
      newState.updateEffort(spec.keys, config);
      newState.updateCost(heuristic(newState, endPos, params.costWeight));
      exploreNewState(std::move(newState));
    }
  };

  // Jump motions (gg, G) - called only when not excluded by boundary
  auto exploreJumpMotion = [&](const MotionState& base, const Motion::JumpMotionSpec& spec) {
    MotionState newState = base;
    newState.applySingleMotion(spec.cmd, navContext, lines);
    newState.updateEffort(spec.keys, config);
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Count motions with known position - these are pre-computed so we trust them
  auto exploreMotionCntTimesWithKnownPosition = [&](const MotionState& base, const string& motion, int cnt, const Position& newPos) {
    // Count searches are pre-computed by BufferIndex - they find exact positions
    // within the buffer, so no additional boundary checking needed
    MotionState newState = base;
    newState.applyMotionWithKnownPosition(motion, cnt, newPos);
    newState.updateEffort(
      makeCountedKeys(abs(cnt), rawMotionToKeys.at(motion)),
      config
    );
    newState.updateCost(heuristic(newState, endPos, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // F-motions with known column - same-line so no escape risk
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
          VimCore::generateFMotions<true>(pos.col, endPos.col, lines[pos.line], params.fMotionThreshold),
          'f', ';'
        );
      }
      else {
        handleFMotions(
          VimCore::generateFMotions<false>(pos.col, endPos.col, lines[pos.line], params.fMotionThreshold),
          'F', ';'
        );
      }

      // Count searchable (same line) motions
      for(const auto& motionPair : COUNT_SEARCHABLE_MOTIONS_LINE) {
        string motion = forward ? motionPair.forward : motionPair.backward;
        // Need keys from rawMotionToKeys for counted motion construction
        auto keyIt = rawMotionToKeys.find(motion);
        if (keyIt == rawMotionToKeys.end()) continue;

        LandingType type = motionPair.type;
        array<RepeatMotionResult, 2> countSearchResults = bufferIndex.getTwoClosest( type, pos, endPos);
        for(const auto& cres : countSearchResults) {
          if(!cres.valid()) continue;
          // When bounded, only allow positions strictly inside the region
          // Edge lines may have been affected by clamping
          if (isBounded) {
            if (boundary.hasLinesAbove() && cres.pos.line == 0) continue;
            if (boundary.hasLinesBelow() && cres.pos.line == lines.lastLine()) continue;
          }
          exploreMotionCntTimesWithKnownPosition(s, motion, cres.count, cres.pos);
        }
      }
    }
    // -------------------- END isSameLine --------------------

    // -------------------- START global search --------------------
    // Direct iteration over specs - all supported motions explored automatically
    for (const auto& spec : Motion::SIMPLE_LINE_MOTIONS) {
      exploreSimpleLineMotion(s, spec);
    }
    for (const auto& spec : Motion::VERTICAL_MOTIONS) {
      exploreVerticalMotion(s, spec);
    }
    for (const auto& spec : Motion::WORD_MOTIONS) {
      exploreWordMotion(s, spec);
    }
    for (const auto& spec : Motion::PARAGRAPH_MOTIONS) {
      exploreParagraphMotion(s, spec);
    }
    for (const auto& spec : Motion::SENTENCE_MOTIONS) {
      exploreSentenceMotion(s, spec);
    }
    for (const auto& spec : Motion::SCROLL_MOTIONS) {
      exploreScrollMotion(s, spec);
    }
    // Jump motions - conditional on boundary
    if (!boundary.hasLinesAbove()) {
      exploreJumpMotion(s, Motion::JUMP_MOTIONS[0]);  // gg
    }
    if (!boundary.hasLinesBelow()) {
      exploreJumpMotion(s, Motion::JUMP_MOTIONS[1]);  // G
    }

    // Count searchable motions (paragraph/sentence)
    for(const auto& motionPair : COUNT_SEARCHABLE_MOTIONS_GLOBAL) {
      string motion = forward ? motionPair.forward : motionPair.backward;
      // Need keys from rawMotionToKeys for counted motion construction
      auto keyIt = rawMotionToKeys.find(motion);
      if (keyIt == rawMotionToKeys.end()) continue;

      LandingType type = motionPair.type;
      array<RepeatMotionResult, 2> countSearchResults = bufferIndex.getTwoClosest(type, pos, endPos);
      for(const auto& cres : countSearchResults) {
        if(!cres.valid()) continue;

        // When bounded, only allow positions strictly inside the region
        // Edge lines may have been affected by clamping
        if (boundary.hasLinesAbove() && cres.pos.line == 0) continue;
        if (boundary.hasLinesBelow() && cres.pos.line == lines.lastLine()) continue;

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

vector<RangeResult> MotionOptimizer::optimizeToRange(
    const Lines& lines,
    const Position& startPos,
    const RunningEffort& startingEffort,
    const Position& rangeFirst,
    const Position& rangeLast,
    const string& userSequence,
    NavContext& navContext,
    bool allowMultiplePerPosition,
    const MotionBoundary& boundary,
    const MotionToKeys& rawMotionToKeys,
    const optional<OptimizerParams>& paramsOverride) {
  // Merge defaults with overrides
  const OptimizerParams params = OptimizerParams::merge(defaultParams, paramsOverride);
  // rawMotionToKeys not used in optimizeToRange - spec-based exploration only

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
    return pos >= rangeFirst && pos <= rangeLast;
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

  // Exploration helper - applies motion and updates state
  auto exploreMotion = [&](const MotionState& base, const char* cmd, const PhysicalKeys& keys) {
    MotionState newState = base;
    newState.applySingleMotion(cmd, navContext, lines);
    newState.updateEffort(keys, config);
    newState.updateCost(heuristicToRange(newState, rangeFirst, rangeLast, params.costWeight));
    exploreNewState(std::move(newState));
  };

  // Start - set cost to heuristic (f = g + h, where g = 0 for fresh start)
  initialState.updateCost(heuristicToRange(initialState, rangeFirst, rangeLast, params.costWeight));
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

    // Direct iteration over specs - all supported motions explored automatically
    for (const auto& spec : Motion::SIMPLE_LINE_MOTIONS) {
      exploreMotion(s, spec.cmd, spec.keys);
    }
    for (const auto& spec : Motion::VERTICAL_MOTIONS) {
      exploreMotion(s, spec.cmd, spec.keys);
    }
    for (const auto& spec : Motion::WORD_MOTIONS) {
      exploreMotion(s, spec.cmd, spec.keys);
    }
    for (const auto& spec : Motion::PARAGRAPH_MOTIONS) {
      exploreMotion(s, spec.cmd, spec.keys);
    }
    for (const auto& spec : Motion::SENTENCE_MOTIONS) {
      exploreMotion(s, spec.cmd, spec.keys);
    }
    for (const auto& spec : Motion::SCROLL_MOTIONS) {
      exploreMotion(s, spec.cmd, spec.keys);
    }
    // Jump motions - conditional on boundary
    if (!boundary.hasLinesAbove()) {
      exploreMotion(s, Motion::JUMP_MOTIONS[0].cmd, Motion::JUMP_MOTIONS[0].keys);  // gg
    }
    if (!boundary.hasLinesBelow()) {
      exploreMotion(s, Motion::JUMP_MOTIONS[1].cmd, Motion::JUMP_MOTIONS[1].keys);  // G
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
