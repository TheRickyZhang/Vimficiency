// TransformOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "TransformOptimizer.h"

#include <algorithm>
#include <cassert>
#include <vector>

#include "ChangeGoalHandler.h"
#include "TransformExplorer.h"
#include "TransformPostExplorerEmissions.h"
#include "TransformTransitionDispatcher.h"
#include "Effort/EffortBank.h"

#include "Keyboard/KeyedSequence.h"

using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

TransformResult::TransformResult(vector<vector<Result>> results, TransformSearchStats stats,
                       const Lines& initialLines, int bufferBeginLine,
                       int bufferBeginCol, CursorPos goalPos,
                       vector<vector<CursorPos>> resultGoalPositions)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      goalPos_(goalPos),
      beginLine_(bufferBeginLine),
      beginCol_(bufferBeginCol),
      resultGoalPositions_(std::move(resultGoalPositions)) {
  if (!resultGoalPositions_.empty()) {
    assert(resultGoalPositions_.size() == results_.size() &&
           "result cursor matrix must match transform result buckets");
    for (size_t i = 0; i < results_.size(); i++) {
      assert(resultGoalPositions_[i].size() == results_[i].size() &&
             "each result cursor bucket must match result count");
    }
  }

  lineBaseIndex_.reserve(initialLines.size());
  int cumSum = 0;
  for (size_t i = 0; i < initialLines.size(); i++) {
    int colOffset = (i == 0) ? beginCol_ : 0;
    lineBaseIndex_.push_back(cumSum - colOffset);
    int positions = initialLines[i].empty() ? 1 : static_cast<int>(initialLines[i].size());
    cumSum += positions;
  }

  for (int line = 0; line < static_cast<int>(initialLines.size()); line++) {
    const int positions = initialLines[line].empty()
        ? 1
        : static_cast<int>(initialLines[line].size());
    const int firstCol = (line == 0) ? beginCol_ : 0;
    for (int localCol = 0; localCol < positions; localCol++) {
      const int bufferCol = firstCol + localCol;
      const int idx = resultIndexAt(bufferBeginLine + line, bufferCol);
      if (idx >= 0 && idx < static_cast<int>(results_.size()) &&
          !results_[static_cast<size_t>(idx)].empty()) {
        startPositions_.push_back(CursorPos(bufferBeginLine + line, bufferCol));
      }
    }
  }

  // Sort each bucket by cost (best first) — pop order is approximate due to
  // inadmissible heuristic and suffix-cache emissions.
  for (size_t i = 0; i < results_.size(); i++) {
    auto& bucket = results_[i];
    if (resultGoalPositions_.empty()) {
      std::sort(bucket.begin(), bucket.end(),
                [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
      continue;
    }

    auto& goalBucket = resultGoalPositions_[i];
    vector<size_t> order(bucket.size());
    for (size_t j = 0; j < order.size(); j++) order[j] = j;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return bucket[a].getCost() < bucket[b].getCost();
    });

    vector<Result> sortedBucket;
    vector<CursorPos> sortedGoals;
    sortedBucket.reserve(bucket.size());
    sortedGoals.reserve(goalBucket.size());
    for (size_t idx : order) {
      sortedBucket.push_back(std::move(bucket[idx]));
      sortedGoals.push_back(goalBucket[idx]);
    }
    bucket = std::move(sortedBucket);
    goalBucket = std::move(sortedGoals);
  }
}

ostream& operator<<(ostream& os, const TransformResult& transformResult) {
  os << transformResult.stats_ << " goalPos=" << transformResult.goalPos_ << "\n";
  for (size_t i = 0; i < transformResult.results_.size(); i++) {
    const auto& bucket = transformResult.results_[i];
    if (!bucket.empty()) {
      os << "  [" << i << "] " << bucket[0] << "\n";
    }
  }
  return os;
}

namespace {

void maybeMarkStartExhausted(const vector<int>& pendingByStart,
                             vector<char>& startActive,
                             int& terminalStarts,
                             int idx) {
  if (!startActive[idx] || pendingByStart[idx] != 0) return;
  startActive[idx] = false;
  terminalStarts++;
}

struct DeletionGoalHandler {
  const string& preSuf;

  DeletionGoalHandler(const Lines&, int, int,
                      double, const Config&,
                      const TransformBoundary&,
                      const Lines&, const Lines&,
                      const string&, const string&, const string& preSuf)
      : preSuf(preSuf) {}

  bool isGoalReached(const Lines& lines) const {
    return lines.size() == 1 && lines[0] == preSuf;
  }

  SuffixCacheResult tryUseSuffixCache(const TransformState&,
                                      vector<vector<Result>>&, int,
                                      const vector<char>&, int&, int&) { return {}; }

  TransformResult finalize(vector<vector<Result>>&& resultsByStart, const Lines& initialLines,
                      const Lines&, const TransformOptimizerParams&,
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
                      TransformSearchStats stats,
                      vector<vector<CursorPos>>&& resultGoalPositions) {
    return TransformResult(
        std::move(resultsByStart), std::move(stats), initialLines,
        bufferBeginLine, bufferBeginCol, goalPos, std::move(resultGoalPositions));
  }
};

} // anonymous namespace

template<bool Capture>
struct PureDeletionGoalCapture;

template<>
struct PureDeletionGoalCapture<false> {
  explicit PureDeletionGoalCapture(int) {}

  void onResult(int, const CursorPos&) {}
  void replaceResult(int, size_t, const CursorPos&) {}

  vector<vector<CursorPos>> takeGoals(int) { return {}; }
};

template<>
struct PureDeletionGoalCapture<true> {
  vector<vector<CursorPos>> goalPosByStart;

  explicit PureDeletionGoalCapture(int totalPositions)
      : goalPosByStart(static_cast<size_t>(totalPositions)) {}

  void onResult(int idx, const CursorPos& pos) {
    goalPosByStart[static_cast<size_t>(idx)].push_back(pos);
  }

  void replaceResult(int idx, size_t resultIndex, const CursorPos& pos) {
    auto& bucket = goalPosByStart[static_cast<size_t>(idx)];
    assert(resultIndex < bucket.size());
    bucket[resultIndex] = pos;
  }

  vector<vector<CursorPos>> takeGoals(int bufferBeginLine) {
    for (auto& bucket : goalPosByStart) {
      for (CursorPos& p : bucket) {
        p.line += bufferBeginLine;
      }
    }
    return std::move(goalPosByStart);
  }
};

// =============================================================================
// =============================================================================
// optimizeImpl - unified template for both pure deletion and full edit
// =============================================================================

template<bool PureDeletion>
TransformResult TransformOptimizer::optimizeImpl(const Lines &initialLines, const Lines &goalLines,
                                       TransformBoundary transformBoundary, TransformOptimizerParams params,
                                       int bufferBeginLine, int bufferBeginCol,
                                       CursorPos goalPos) {
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");
  params.maxResultsPerStartPos = max(1, params.maxResultsPerStartPos);

  const int leftColOffset = transformBoundary.leftColOffset();
  const int rightColOffset = transformBoundary.rightColOffset();
  const double effortWeight = params.effortWeight;
  const double distanceWeight = params.distanceWeight;

  // Effective lines = boundary prefix + edit region + boundary suffix.
  // Same wrapping the depth-1 frontier (TransformFrontier) uses, via the
  // shared `withBoundary` so both stay in lockstep.
  Lines effectiveLines = transformBoundary.withBoundary(initialLines);
  const auto& pre = transformBoundary.prefix();
  const auto& suf = transformBoundary.suffix();

  EffortBank bank(config);
  TransformExplorer explorer(transformBoundary, params, config, bank, leftColOffset, rightColOffset);

  TransformPriorityQueue pq;
  TransformCostMap costMap;
  const int totalPositions = initialLines.totalPositions();
  vector<int> pendingByStart(totalPositions, 0);
  vector<char> startActive(totalPositions, true);

  int terminalStarts = 0;
  int totalPops = 0;
  int resultsFound = 0;
  int uniquePositionsCovered = 0;
  TransformSearchStats stats;

  const double startPriority =
      weightedHeuristicCost(effectiveLines, leftColOffset, rightColOffset, distanceWeight);
  TransformStateFactory states(config, effortWeight);
  int startIndex = 0;
  for (int line = 0; line < static_cast<int>(initialLines.size()); line++) {
    for (int col = 0; col < initialLines[line].effectiveSize(); col++) {
      int effCol = col + (line == 0 ? leftColOffset : 0);
      int effLineLen = static_cast<int>(effectiveLines[line].size());
      if (effLineLen > 0 && effCol >= effLineLen) {
        startIndex++;
        continue;
      }
      pendingByStart[startIndex]++;
      pq.push(states.initial(effectiveLines, CursorPos(line, effCol), startIndex, startPriority));
      startIndex++;
    }
  }
  for (int i = 0; i < totalPositions; i++) {
    if (pendingByStart[i] == 0) {
      startActive[i] = false;
      terminalStarts++;
    }
  }

  const string preSuf = pre + suf;

  vector<vector<Result>> resultsByStart(totalPositions);
  PureDeletionGoalCapture<PureDeletion> goalCapture(totalPositions);

  using ModeType = std::conditional_t<PureDeletion, DeletionGoalHandler, ChangeGoalHandler>;
  static_assert(GoalHandlerCore<ModeType>);
  static_assert(PureDeletion || ChangeGoalEmitter<ModeType>);
  ModeType mode(effectiveLines, leftColOffset, rightColOffset, effortWeight,
                config, transformBoundary, initialLines, goalLines,
                pre, suf, preSuf);

  auto recordResult = [&](const TransformState& state, const Result& result, bool captureGoalPos) {
    int idx = state.getStartIndex();
    if (!startActive[idx]) return;
    auto& bucket = resultsByStart[idx];
    if (static_cast<int>(bucket.size()) >= params.maxResultsPerStartPos) return;

    bool firstForStart = bucket.empty();
    bucket.push_back(result);
    resultsFound++;
    if (captureGoalPos) goalCapture.onResult(idx, state.getPos());
    if (firstForStart) {
      uniquePositionsCovered++;
    }
    if (static_cast<int>(bucket.size()) == params.maxResultsPerStartPos) {
      if (startActive[idx]) {
        startActive[idx] = false;
        terminalStarts++;
      }
    }
  };

  using DispatchType = TransformTransitionDispatcher<PureDeletion, ModeType>;

  while (!pq.empty() &&
         terminalStarts < totalPositions &&
         resultsFound < params.maxResults &&
         totalPops < params.maxNodesPopped) {
    TransformState s = pq.top(); pq.pop();
    totalPops++;
    int activeStart = s.getStartIndex();
    assert(pendingByStart[activeStart] > 0 && "pendingByStart underflow");
    pendingByStart[activeStart]--;

    if (!startActive[activeStart]) continue;

    auto costIt = costMap.find(s.getKey());
    if (costIt != costMap.end() && costIt->second < s.getCost() - 1e-9) {
      stats.incrementStatesSkipped();
      ::maybeMarkStartExhausted(pendingByStart, startActive, terminalStarts, activeStart);
      continue;
    }

    stats.incrementNodesExplored();
    const CursorPos pos = s.getPos();
    const Lines& lines = s.getLines();
    stats.maybeRecordExploredState(pos.line, pos.col, s.getEffort(), string_view(s.getSeq()));

    if (mode.isGoalReached(lines)) {
      recordResult(s, Result(s.getSeq(), s.getEffort()), true);
      ::maybeMarkStartExhausted(pendingByStart, startActive, terminalStarts, activeStart);
      continue;
    }

    assert(pos.line >= 0 && pos.line <= lines.lastLine() &&
           "non-goal edit state must have in-bounds cursor line");

    auto cacheResult = mode.tryUseSuffixCache(s, resultsByStart,
                                              params.maxResultsPerStartPos,
                                              startActive,
                                              resultsFound,
                                              uniquePositionsCovered);
    if (cacheResult.hit) {
      if (cacheResult.startCapped && startActive[activeStart]) {
        startActive[activeStart] = false;
        terminalStarts++;
      }
      ::maybeMarkStartExhausted(pendingByStart, startActive, terminalStarts, activeStart);
      continue;
    }
    DispatchType dispatch{
        mode,         s,           stats,         pq,
        costMap,      pendingByStart, startActive, bank,
        transformBoundary, config,      effortWeight, distanceWeight,
        leftColOffset, rightColOffset};

    auto onDeletion = [&](const auto& range, const SequenceBinding& cmd) { dispatch.deleteRange(range, cmd); };
    auto onLinewise = [&](LineRange r, const SequenceBinding& cmd) { dispatch.deleteLinewise(r, cmd); };
    auto onJoin = [&](bool sp, const SequenceBinding& cmd) { dispatch.joinLines(sp, cmd); };

    bool inSuffixRegion = pos.line == lines.lastLine() &&
                          rightColOffset > 0 &&
                          pos.col + rightColOffset >= static_cast<int>(lines.getSize(pos.line));
    if (inSuffixRegion) {
      int firstSuffixCol = static_cast<int>(lines.getSize(pos.line)) - rightColOffset;
      if (pos.col == firstSuffixCol && firstSuffixCol > 0) {
        explorer.exploreWordEdits(
            Edit::EXCLUSIVE_BACKWARD_WORD_EDITS, pos, lines, onDeletion,
            onLinewise);
      }
      if (pos.col > 0) {
        dispatch.moveByMotion(CursorPos(pos.line, pos.col - 1),
                              SequenceBinding(KeyedSequence::h, bank[KSId::h]));
      }
      if (pos.line > 0) {
        int newCol = VimCore::clampCol(lines, pos.targetCol, pos.line - 1);
        dispatch.moveByMotion(CursorPos(pos.line - 1, newCol, pos.targetCol),
                              SequenceBinding(KeyedSequence::k, bank[KSId::k]));
      }
      continue;
    }
    if (pos.line == 0 && pos.col < leftColOffset) {
      if (pos.col < static_cast<int>(lines[0].size()) - 1) {
        dispatch.moveByMotion(CursorPos(0, pos.col + 1),
                              SequenceBinding(KeyedSequence::l, bank[KSId::l]));
      }
      if (lines.lastLine() > 0) {
        int newCol = VimCore::clampCol(lines, pos.targetCol, 1);
        dispatch.moveByMotion(CursorPos(1, newCol, pos.targetCol),
                              SequenceBinding(KeyedSequence::j, bank[KSId::j]));
      }
      continue;
    }

    sweepExplorerStructurals(
        explorer, s, lines, pos, leftColOffset, rightColOffset,
        params.minPrefixCount,
        onDeletion, onLinewise, onJoin,
        [&](LineRange range, const SequenceBinding& sourceCmd) {
          dispatch.deleteCountedLinewise(range, sourceCmd);
        },
        [&](bool addSpace, const SequenceBinding& sourceCmd) {
          dispatch.joinCountedLines(addSpace, sourceCmd);
        });
    ::maybeMarkStartExhausted(pendingByStart, startActive, terminalStarts, activeStart);
  }

  stats.finalize(stats.nodesExplored(),
                            totalPops,
                            resultsFound,
                            uniquePositionsCovered,
                            static_cast<int>(pq.size()),
                            terminalStarts,
                            totalPositions,
                            params.maxResults,
                            params.maxNodesPopped,
                            pq.empty());

  if constexpr (PureDeletion) {
    auto visual = TransformPostExplorer::tryVisualDelete(
        effectiveLines, leftColOffset, rightColOffset,
        transformBoundary, params, config);
    if (visual) {
      auto& bucket = resultsByStart[0];
      if (bucket.empty()) {
        bucket.push_back(std::move(visual->result));
        goalCapture.onResult(0, visual->goalPos);
      } else {
        auto bestIt = std::min_element(
            bucket.begin(), bucket.end(),
            [](const Result& a, const Result& b) {
              return a.getCost() < b.getCost();
            });
        if (visual->result.getCost() < bestIt->getCost()) {
          size_t resultIndex = static_cast<size_t>(bestIt - bucket.begin());
          *bestIt = std::move(visual->result);
          goalCapture.replaceResult(0, resultIndex, visual->goalPos);
        }
      }
    }
  }

  return mode.finalize(
      std::move(resultsByStart), initialLines, goalLines, params,
      bufferBeginLine, bufferBeginCol, goalPos, std::move(stats),
      goalCapture.takeGoals(bufferBeginLine));
}
// Explicit template instantiations
template TransformResult TransformOptimizer::optimizeImpl<false>(
    const Lines&, const Lines&, TransformBoundary, TransformOptimizerParams, int, int, CursorPos);
template TransformResult TransformOptimizer::optimizeImpl<true>(
    const Lines&, const Lines&, TransformBoundary, TransformOptimizerParams, int, int, CursorPos);

// =============================================================================
// Public dispatchers
// =============================================================================


TransformResult
TransformOptimizer::optimizeTransform(
    const Lines &initialLines, const Lines &goalLines,
    TransformBoundary transformBoundary, TransformOptimizerParams params,
    int bufferBeginLine, int bufferBeginCol, CursorPos goalPos) {
  assert(initialLines != goalLines);
  assert(!initialLines.empty());
  bool pureDeletionGoal = goalLines.empty() ||
                          (goalLines.size() == 1 && goalLines[0].empty());
  assert(!pureDeletionGoal &&
         "optimizeTransform does not accept empty goalLines; use optimizePureDeletion for pure deletions");
  return optimizeImpl<false>(initialLines, goalLines, transformBoundary, params,
                             bufferBeginLine, bufferBeginCol, goalPos);
}

TransformResult TransformOptimizer::optimizePureDeletion(
    const Lines& initialLines,
    TransformBoundary transformBoundary,
  TransformOptimizerParams params,
  int bufferBeginLine,
  int bufferBeginCol,
  CursorPos goalPos) {
  assert(!initialLines.empty());
  return optimizeImpl<true>(
      initialLines, Lines{}, transformBoundary, params,
      bufferBeginLine, bufferBeginCol, goalPos);
}
