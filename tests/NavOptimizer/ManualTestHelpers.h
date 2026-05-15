#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Boundary/NavBoundary.h"
#include "Effort/RunningEffort.h"
#include "Interpreter/MovementInterpreter.h"
#include "Keyboard/Config.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Optimizer/NavOptimizer/BufferIndex.h"
#include "Optimizer/NavOptimizer/NavExplorer.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Optimizer/NavOptimizer/NavRangeConversion.h"
#include "Session/Snapshot.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"
#include "Utils/TestUtils.h"

class NavOptimizer_ManualTest : public ::testing::Test {
protected:
  inline static Lines a1_long_line;
  inline static Lines a2_block_lines;
  inline static Lines a3_spaced_lines;
  inline static Lines m1_main_basic;
  inline static NavContext navContext;

  static void SetUpTestSuite() {
    a1_long_line = TestFiles::load("a1_long_line.txt");
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    a3_spaced_lines = TestFiles::load("a3_spaced_lines.txt");
    m1_main_basic = TestFiles::load("m1_main_basic.txt");
    navContext = NavContext();
  }

  static std::vector<LandingResult>
  runOptimizer(const Lines& lines, CursorPos start,
               CursorPos end, const std::string& userSeq,
               std::vector<KeyAdjustment> adjustments = {},
               Config config = Config::uniform()) {
    for (KeyAdjustment ka : adjustments) {
      config.keyInfo[static_cast<size_t>(ka.k)].base_cost = ka.cost;
    }

    NavOptimizer opt(config);
    NavBoundary boundary;
    return opt.optimize(lines, start, end,
                        NavOptimizerParams{}
                            .withMaxResults(30)
                            .withMaxNodesPopped(20000)
                            .withMaxResultsPerEndPos(2),
                        userSeq, boundary, navContext).getResults();
  }

  static std::vector<LandingResult>
  runOptimizerToRange(const Lines& lines, CursorPos start,
                      CursorPos rangeBegin, CursorPos rangeEnd,
                      const std::string& userSeq,
                      int maxResults = 10,
                      Config config = Config::uniform()) {
    NavOptimizer opt(config);
    NavBoundary boundary;
    return opt.optimize(lines, start,
                        CharInterval(CharRange(rangeBegin, rangeEnd), lines),
                        NavOptimizerParams{}
                            .withMaxResults(maxResults)
                            .withMaxNodesPopped(20000)
                            .withMaxResultsPerEndPos(2),
                        userSeq, boundary, navContext).getResults();
  }
};

class NavBoundaryTest : public ::testing::Test {
protected:
  struct CountedCandidate {
    std::string sequence;
    CursorPos endpoint;
  };

  struct StaticCandidate {
    std::string sequence;
    CursorPos endpoint;
  };

  inline static NavContext navContext;

  static void SetUpTestSuite() {
    navContext = NavContext();
  }

  static std::vector<LandingResult>
  runWithBoundary(const Lines& lines, CursorPos start, CursorPos end,
                  const std::string& userSeq, const NavBoundary& boundary,
                  Config config = Config::uniform()) {
    NavOptimizer opt(config);
    return opt.optimize(lines, start, end,
                        NavOptimizerParams{}
                            .withMaxResults(30)
                            .withMaxNodesPopped(20000)
                            .withMaxResultsPerEndPos(2),
                        userSeq, boundary, navContext).getResults();
  }

  static bool hasSequence(const std::vector<LandingResult>& results,
                          const std::string& seq) {
    return std::any_of(results.begin(), results.end(),
        [&seq](const Result& r) { return r.getSequence() == seq; });
  }

  static std::vector<CountedCandidate> collectCountedCandidates(
      const Lines& lines,
      CursorPos start,
      CursorPos goal,
      const NavBoundary& boundary,
      NavOptimizerParams params = NavOptimizerParams{}
          .withMinCountRepeat(2)
          .withMaxCountRepeat(8)) {
    Config config = Config::uniform();
    BufferIndex index(lines);
    CharInterval goalRange(goal, goal);
    NavExplorer explorer(lines, navContext, boundary, params, goalRange, index, 0);

    auto score = [](CursorPos, double effort) { return effort; };
    NavStateFactory states(config, score);
    NavState base = states.initial(start);

    std::vector<CountedCandidate> candidates;
    auto onCounted = [&](KSId, const KeyedSequence& ks, int count,
                         CursorPos endpoint, double) {
      KeyedSequence counted(count, ks);
      candidates.push_back({counted.seq.str(), endpoint});
    };
    auto onFMotion = [](const KeyedSequence&, int) {};
    explorer.exploreCountedMotions(base, onCounted, onFMotion);
    return candidates;
  }

  static std::vector<StaticCandidate> collectStaticCandidates(
      const Lines& lines,
      CursorPos start,
      CursorPos goal,
      const NavBoundary& boundary,
      NavOptimizerParams params = NavOptimizerParams{}) {
    Config config = Config::uniform();
    BufferIndex index(lines);
    CharInterval goalRange(goal, goal);
    NavExplorer explorer(lines, navContext, boundary, params, goalRange, index, 0);

    auto score = [](CursorPos, double effort) { return effort; };
    NavStateFactory states(config, score);
    NavState base = states.initial(start);

    std::vector<StaticCandidate> candidates;
    auto onStatic = [&](KSId, const KeyedSequence& ks, CursorPos endpoint) {
      candidates.push_back({ks.seq.str(), endpoint});
    };
    explorer.exploreAllStandardMotions(base, onStatic);
    return candidates;
  }

  static bool hasCountedCandidate(
      const std::vector<CountedCandidate>& candidates,
      const std::string& sequence,
      CursorPos endpoint) {
    return std::any_of(candidates.begin(), candidates.end(),
        [&](const CountedCandidate& candidate) {
          return candidate.sequence == sequence && candidate.endpoint == endpoint;
        });
  }

  static bool hasStaticCandidate(
      const std::vector<StaticCandidate>& candidates,
      const std::string& sequence) {
    return std::any_of(candidates.begin(), candidates.end(),
        [&](const StaticCandidate& candidate) {
          return candidate.sequence == sequence;
        });
  }
};
