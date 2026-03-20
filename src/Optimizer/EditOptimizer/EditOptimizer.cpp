// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include <algorithm>
#include <vector>

#include "ChangeGoalHandler.h"
#include "EditExplorer.h"
#include "EditTransitionDispatcher.h"
#include "Effort/EffortBank.h"

#include "Keyboard/PhysicalKeys.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/ToKeys/MotionToKeys.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"

using namespace std;

// =============================================================================
// Internal Helpers
// =============================================================================

EditResult::EditResult(vector<vector<Result>> results, EditSearchStats stats,
                       const Lines& initialLines, int bufferBeginLine,
                       int bufferBeginCol, CursorPos goalPos)
    : BaseOptimizerResult(std::move(results)),
      stats_(std::move(stats)),
      goalPos_(goalPos),
      beginLine_(bufferBeginLine),
      beginCol_(bufferBeginCol) {
  lineBaseIndex_.reserve(initialLines.size());
  int cumSum = 0;
  for (size_t i = 0; i < initialLines.size(); i++) {
    int colOffset = (i == 0) ? beginCol_ : 0;
    lineBaseIndex_.push_back(cumSum - colOffset);
    int positions = initialLines[i].empty() ? 1 : static_cast<int>(initialLines[i].size());
    cumSum += positions;
  }

  // Sort each bucket by cost (best first) — pop order is approximate due to
  // inadmissible heuristic and suffix-cache emissions.
  for (auto& bucket : results_) {
    std::sort(bucket.begin(), bucket.end(),
              [](const Result& a, const Result& b) { return a.getCost() < b.getCost(); });
  }
}

ostream& operator<<(ostream& os, const EditResult& editResult) {
  os << editResult.stats_ << " goalPos=" << editResult.goalPos_ << "\n";
  for (size_t i = 0; i < editResult.results_.size(); i++) {
    const auto& bucket = editResult.results_[i];
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
  const Lines& effectiveLines;
  int leftColOffset;
  int rightColOffset;
  const Config& config;
  const string& preSuf;

  DeletionGoalHandler(const Lines& effectiveLines, int leftColOffset, int rightColOffset,
                      double, const Config& config,
                      const EditBoundary&, const Lines&, const Lines&,
                      const string&, const string&, const string& preSuf)
      : effectiveLines(effectiveLines),
        leftColOffset(leftColOffset),
        rightColOffset(rightColOffset),
        config(config),
        preSuf(preSuf) {}

  bool isGoalReached(const Lines& lines) const {
    return lines.size() == 1 && lines[0] == preSuf;
  }

  SuffixCacheResult tryUseSuffixCache(const EditState&,
                                      vector<vector<Result>>&, int,
                                      const vector<char>&, int&, int&) { return {}; }

  EditResult finalize(vector<vector<Result>>&& resultsByStart, const Lines& initialLines,
                      const Lines&, const EditOptimizerParams& params,
                      int bufferBeginLine, int bufferBeginCol, CursorPos goalPos,
                      EditSearchStats stats) {
    if (effectiveLines.size() > 1 ||
        static_cast<int>(effectiveLines[0].size()) > leftColOffset + rightColOffset) {
      CursorPos beginPos(0, leftColOffset);
      int lastLine = effectiveLines.lastLine();
      int lastCol = static_cast<int>(effectiveLines[lastLine].size()) - 1 - rightColOffset;
      CursorPos lastPos(lastLine, max(0, lastCol));

      if (lastPos > beginPos || (lastPos.line == beginPos.line && lastPos.col > beginPos.col)) {
        MotionOptimizer motionOpt(config);

        auto motionResult = motionOpt.optimize(
            effectiveLines,
            beginPos,
            lastPos,
            MotionOptimizerParams{}
                .withLinePaddingAbove(params.motionLinePaddingAbove)
                .withLinePaddingBelow(params.motionLinePaddingBelow)
                .withMinCountRepeat(params.minPrefixCount)
                .withMaxCountRepeat(params.maxPrefixCount)
        );

        const auto& motionResults = motionResult.getResults();
        if (!motionResults.empty() && motionResults[0].isValid()) {
          Sequence visualSeq("v");
          visualSeq.append(motionResults[0].getSequence().view());
          visualSeq.append("d");

          static const PhysicalKeys vKey = {Key::Key_V};
          static const PhysicalKeys dKey = {Key::Key_D};
          RunningEffort effort(vKey, config);
          effort.append(globalSequenceToKeys().tokenize(motionResults[0].getSequence().view()), config);
          double totalEffort = effort.append(dKey, config);

          auto& bucket = resultsByStart[0];
          if (bucket.empty() || totalEffort < bucket[0].getCost()) {
            if (bucket.empty()) {
              bucket.emplace_back(std::move(visualSeq), totalEffort);
            } else {
              bucket[0] = Result(std::move(visualSeq), totalEffort);
            }
          }
        }
      }
    }

    return EditResult(std::move(resultsByStart), std::move(stats), initialLines,
                      bufferBeginLine, bufferBeginCol, goalPos);
  }
};

} // anonymous namespace

template<bool Capture>
struct PureDeletionGoalCapture;

template<>
struct PureDeletionGoalCapture<false> {
  explicit PureDeletionGoalCapture(int) {}

  void onGoal(int, const CursorPos&) {}

  EditResult finalize(EditResult&& editResult, int) {
    return std::move(editResult);
  }
};

template<>
struct PureDeletionGoalCapture<true> {
  vector<CursorPos> goalPosByStart;

  explicit PureDeletionGoalCapture(int totalPositions)
      : goalPosByStart(static_cast<size_t>(totalPositions), CursorPos(-1, -1, -1)) {}

  void onGoal(int idx, const CursorPos& pos) {
    goalPosByStart[static_cast<size_t>(idx)] = pos;
  }

  EditResult finalize(EditResult&& editResult, int bufferBeginLine) {
    for (CursorPos& p : goalPosByStart) {
      if (p.line < 0) continue;
      p.line += bufferBeginLine;
    }
    editResult.setGoalPosByStart(std::move(goalPosByStart));
    return std::move(editResult);
  }
};

// =============================================================================
// =============================================================================
// optimizeImpl - unified template for both pure deletion and full edit
// =============================================================================

template<bool PureDeletion>
EditResult EditOptimizer::optimizeImpl(const Lines &initialLines, const Lines &goalLines,
                                       EditBoundary editBoundary, EditOptimizerParams params,
                                       int bufferBeginLine, int bufferBeginCol,
                                       CursorPos goalPos) {
  assert(!initialLines.empty() && "empty startlines should be handled in compositionEditor by i, a, o, O");
  params.maxMultiplePerStartPosition = max(1, params.maxMultiplePerStartPosition);

  const int leftColOffset = editBoundary.leftColOffset();
  const int rightColOffset = editBoundary.rightColOffset();
  const double effortWeight = params.effortWeight;
  const double distanceWeight = params.distanceWeight;

  Lines effectiveLines = initialLines;
  const auto& pre = editBoundary.prefix();
  const auto& suf = editBoundary.suffix();
  if (!pre.empty()) effectiveLines.front().insert(0, pre);
  if (!suf.empty()) effectiveLines.back() += suf;

  EffortBank bank(config);
  EditExplorer explorer(editBoundary, params, config, bank, leftColOffset, rightColOffset);

  EditPriorityQueue pq;
  EditCostMap costMap;
  const int totalPositions = initialLines.totalPositions();
  vector<int> pendingByStart(totalPositions, 0);
  vector<char> startActive(totalPositions, true);

  int terminalStarts = 0;
  int totalPops = 0;
  int resultsFound = 0;
  int uniquePositionsCovered = 0;
  EditSearchStats stats;

  const double startPriority =
      weightedHeuristicCost(effectiveLines, leftColOffset, rightColOffset, distanceWeight);
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
      pq.push(EditState(effectiveLines, CursorPos(line, effCol), startIndex, startPriority));
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
                config, editBoundary, initialLines, goalLines,
                pre, suf, preSuf);

  auto recordResult = [&](const EditState& state, const Result& result, bool captureGoalPos) {
    int idx = state.getStartIndex();
    if (!startActive[idx]) return;
    auto& bucket = resultsByStart[idx];
    if (static_cast<int>(bucket.size()) >= params.maxMultiplePerStartPosition) return;

    bool firstForStart = bucket.empty();
    bucket.push_back(result);
    resultsFound++;
    if (firstForStart) {
      uniquePositionsCovered++;
      if (captureGoalPos) goalCapture.onGoal(idx, state.getPos());
    }
    if (static_cast<int>(bucket.size()) == params.maxMultiplePerStartPosition) {
      if (startActive[idx]) {
        startActive[idx] = false;
        terminalStarts++;
      }
    }
  };

  using DispatchType = EditTransitionDispatcher<PureDeletion, ModeType>;

  while (!pq.empty() &&
         terminalStarts < totalPositions &&
         resultsFound < params.maxResults &&
         totalPops < params.maxNodesPopped) {
    EditState s = pq.top(); pq.pop();
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
                                              params.maxMultiplePerStartPosition,
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
        editBoundary, config,      effortWeight, distanceWeight,
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
        explorer.exploreBackwardWordEdits<EdgeType::WordEdge>(
            Edit::BACKWARD_WORDEDGE_EDITS, pos, lines, onDeletion);
        explorer.exploreBackwardWordEdits<EdgeType::NextEdge>(
            Edit::BACKWARD_NEXTEDGE_EDITS, pos, lines, onDeletion);
      }
      if (pos.col > 0) {
        dispatch.moveByMotion(CursorPos(pos.line, pos.col - 1),
                              SequenceBinding(KeyedSequence::h, bank[KSId::h]));
      }
      if (pos.line > 0) {
        int newCol = min(pos.targetCol, lines[pos.line - 1].lastCol());
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
        int newCol = min(pos.targetCol, lines[1].lastCol());
        dispatch.moveByMotion(CursorPos(1, newCol, pos.targetCol),
                              SequenceBinding(KeyedSequence::j, bank[KSId::j]));
      }
      continue;
    }

    explorer.exploreAllDeletions(s, onDeletion, onLinewise, onJoin);
    explorer.exploreCountedLineEdits(
        pos, lines, params.minPrefixCount,
        [&](LineRange range, const SequenceBinding& sourceCmd) {
          dispatch.deleteCountedLinewise(range, sourceCmd);
        });
    explorer.exploreCountedJoinCommands(
        pos, lines, params.minPrefixCount,
        [&](bool addSpace, const SequenceBinding& sourceCmd) {
          dispatch.joinCountedLines(addSpace, sourceCmd);
        });
    explorer.exploreCountedWordEdits(pos, lines, params.minPrefixCount, onDeletion);

    int contentStart = (pos.line == 0) ? leftColOffset : 0;
    int contentEnd = static_cast<int>(lines[pos.line].size());
    if (pos.line == lines.lastLine() && rightColOffset > 0) {
      contentEnd -= rightColOffset;
    }
    explorer.exploreCountedCharEdits(pos, lines, contentStart, contentEnd,
                                     params.minPrefixCount, onDeletion);
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

  return goalCapture.finalize(
      mode.finalize(std::move(resultsByStart), initialLines, goalLines, params,
                    bufferBeginLine, bufferBeginCol, goalPos, std::move(stats)),
      bufferBeginLine);
}
// Explicit template instantiations
template EditResult EditOptimizer::optimizeImpl<false>(
    const Lines&, const Lines&, EditBoundary, EditOptimizerParams, int, int, CursorPos);
template EditResult EditOptimizer::optimizeImpl<true>(
    const Lines&, const Lines&, EditBoundary, EditOptimizerParams, int, int, CursorPos);

// =============================================================================
// Public dispatchers
// =============================================================================


EditResult
EditOptimizer::optimizeEdit(
    const Lines &initialLines, const Lines &goalLines,
    EditBoundary editBoundary, EditOptimizerParams params,
    int bufferBeginLine, int bufferBeginCol, CursorPos goalPos) {
  assert(initialLines != goalLines);
  assert(!initialLines.empty());
  bool pureDeletionGoal = goalLines.empty() ||
                          (goalLines.size() == 1 && goalLines[0].empty());
  assert(!pureDeletionGoal &&
         "optimizeEdit does not accept empty goalLines; use optimizePureDeletion for pure deletions");
  return optimizeImpl<false>(initialLines, goalLines, editBoundary, params,
                             bufferBeginLine, bufferBeginCol, goalPos);
}

EditResult EditOptimizer::optimizePureDeletion(
    const Lines& initialLines,
    EditBoundary editBoundary,
  EditOptimizerParams params,
  int bufferBeginLine,
  int bufferBeginCol,
  CursorPos goalPos) {
  assert(!initialLines.empty());
  return optimizeImpl<true>(
      initialLines, Lines{}, editBoundary, params,
      bufferBeginLine, bufferBeginCol, goalPos);
}
