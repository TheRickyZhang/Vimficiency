#pragma once

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

#include "Boundary/MotionBoundary.h"
#include "BufferIndex.h"
#include "MotionClassMask.h"
#include "MotionOptimizerParams.h"
#include "MotionToSpec.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Optimizer/MotionOptimizer/MotionState.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/ToKeys/MotionToKeys.h"
#include "Types/CharInterval.h"
#include "Types/NavContext.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimOptions.h"

// MotionExplorer encapsulates motion candidate generation for A* search.
// It owns only read-only optimization inputs and streams transition intents
// to caller-provided callbacks.
class MotionExplorer {
  enum class GoalSide {
    Before,
    InRange,
    After,
  };

  struct HorizontalBounds {
    int lastCol;
    int left;
    int right;
  };

  struct DirectionalBoundary {
    int edgeOffset;
    bool hasLinesOutside;
  };

  struct PrefixRange {
    int min;
    int max;
  };

public:
  MotionExplorer(const Lines& lines,
                 const NavContext& navContext,
                 const MotionBoundary& boundary,
                 const MotionOptimizerParams& params,
                 const CharInterval& goalRange,
                 const BufferIndex& bufferIndex,
                 int lineOffset)
      : lines_(lines),
        navContext_(navContext),
        boundary_(boundary),
        params_(params),
        goalRange_(goalRange),
        bufferIndex_(bufferIndex),
        bufferLineOffset_(lineOffset) {}

  template<class OnStatic>
  void exploreAllStandardMotions(const MotionState& base, OnStatic&& onStatic) {
    exploreHorizontalMotions(base, onStatic);
    exploreVerticalMotions(base, onStatic);
    exploreWordMotions<true, EdgeType::NextEdge>(Motion::FORWARD_NEXTEDGE_MOTIONS, base, onStatic);
    exploreWordMotions<true, EdgeType::WordEdge>(Motion::FORWARD_WORDEDGE_MOTIONS, base, onStatic);
    exploreWordMotions<false, EdgeType::WordEdge>(Motion::BACKWARD_WORDEDGE_MOTIONS, base, onStatic);
    exploreWordMotions<false, EdgeType::NextEdge>(Motion::BACKWARD_NEXTEDGE_MOTIONS, base, onStatic);
    exploreParagraphMotions<true>(Motion::FORWARD_PARAGRAPH_MOTIONS, base, onStatic);
    exploreParagraphMotions<false>(Motion::BACKWARD_PARAGRAPH_MOTIONS, base, onStatic);
    exploreSentenceMotions<true>(Motion::FORWARD_SENTENCE_MOTIONS, base, onStatic);
    exploreSentenceMotions<false>(Motion::BACKWARD_SENTENCE_MOTIONS, base, onStatic);
    exploreScrollMotions(base, onStatic);
    exploreJumpMotions(base, onStatic);
  }

  template<class OnStatic>
  void exploreDirectionalStandardMotions(const MotionState& base, OnStatic&& onStatic) {
    CursorPos pos = base.getPos();
    MotionClassMask m = classesForRange(pos, goalRange_.first, goalRange_.last);
    exploreClasses(base, m, onStatic);
  }

  template<class OnCounted, class OnFMotion>
  void exploreCountedMotions(const MotionState& base,
                             OnCounted&& onCounted,
                             OnFMotion&& onFMotion) {
    CursorPos pos = base.getPos();
    GoalSide side = goalSide(pos);

    if (side == GoalSide::InRange) return;

    bool lineLocal = isOnGoalEdgeLine(pos.line);
    if (side == GoalSide::Before) {
      exploreCountedMotionsForDirection<true>(base, lineLocal, onCounted, onFMotion);
    } else {
      exploreCountedMotionsForDirection<false>(base, lineLocal, onCounted, onFMotion);
    }
  }

private:

  const Lines& lines_;
  const NavContext& navContext_;
  const MotionBoundary& boundary_;
  const MotionOptimizerParams& params_;
  const CharInterval& goalRange_;
  const BufferIndex& bufferIndex_;
  int bufferLineOffset_ = 0;

  HorizontalBounds horizontalBoundsForLine(int line) const {
    return {
        lines_[line].lastCol(),
        (line == 0) ? boundary_.leftColOffset() : 0,
        (line == lines_.lastLine()) ? boundary_.rightColOffset() : 0,
    };
  }

  DirectionalBoundary boundaryForDirection(bool forward) const {
    return {
        forward ? boundary_.rightColOffset() : boundary_.leftColOffset(),
        forward ? boundary_.hasLinesBelow() : boundary_.hasLinesAbove(),
    };
  }

  int firstNonBlankCol(int line) const {
    return VimCore::firstNonBlankColInLineStr(lines_[line]);
  }

  int clampTargetColToLine(int targetCol, int line) const {
    return VimCore::clampCol(lines_, targetCol, line);
  }

  int maxLineIndex() const {
    return lines_.lastLine();
  }

  bool hasLinesAboveBoundary() const {
    return boundary_.hasLinesAbove();
  }

  bool hasLinesBelowBoundary() const {
    return boundary_.hasLinesBelow();
  }

  bool canLandOnLine(int line) const {
    if (boundary_.hasLinesAbove() && line == 0) return false;
    if (boundary_.hasLinesBelow() && line == lines_.lastLine()) return false;
    return true;
  }

  bool isValidLocalLandingLine(int line) const {
    return line >= 0 && line <= lines_.lastLine() && canLandOnLine(line);
  }

  int scrollShift(bool isHalfMotion, bool forward) const {
    int baseShift = isHalfMotion
        ? navContext_.scrollAmount
        : std::max(0, navContext_.windowHeight - 2);
    return forward ? baseShift : -baseShift;
  }

  std::optional<PrefixRange> boundedPrefixRange(int cappedMax) const {
    int maxBound = std::min(cappedMax, params_.maxPrefixCount);
    if (maxBound < params_.minPrefixCount) return std::nullopt;
    return PrefixRange{params_.minPrefixCount, maxBound};
  }

  bool isValidPrefixCount(int count) const {
    return count >= params_.minPrefixCount && count <= params_.maxPrefixCount;
  }

  GoalSide goalSide(const CursorPos& pos) const {
    if (pos < goalRange_.first) return GoalSide::Before;
    if (pos > goalRange_.last) return GoalSide::After;
    return GoalSide::InRange;
  }

  bool isOnGoalEdgeLine(int line) const {
    return line == goalRange_.first.line || line == goalRange_.last.line;
  }

  bool canExploreFMotionsFrom(const CursorPos& pos) const {
    bool singleGoalLine = (goalRange_.first.line == goalRange_.last.line);
    return singleGoalLine && pos.line == goalRange_.first.line;
  }

  int goalColForDirection(bool forward) const {
    return forward ? goalRange_.last.col : goalRange_.first.col;
  }

  int fMotionThreshold() const {
    return params_.fMotionThreshold;
  }

  template<class OnStatic>
  void emitMotion(KSId id, CursorPos endpoint, OnStatic& onStatic) const {
    onStatic(id, KeyedSequence::byId(id), endpoint);
  }

  template<CountClass C, class OnCounted>
  void exploreCountMotion(KSId baseMotion,
                          int cnt,
                          const CursorPos& newPos,
                          OnCounted& onCounted) const {
    assert(cnt >= 0 && cnt <= CountPrefixLimits::MAX_PREFIX_COUNT);
    CountPenaltyInput in;
    in.count = cnt;
    in.span = cnt;
    double penalty = runtimeCountPenalty<C>(in);
    onCounted(baseMotion, KeyedSequence::byId(baseMotion), cnt, newPos, penalty);
  }

  template<class OnStatic>
  void exploreHorizontalMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    auto bounds = horizontalBoundsForLine(pos.line);

    if (pos.col > bounds.left)
      emitMotion(KSId::h, {pos.line, pos.col - 1}, onStatic);

    if (pos.col < bounds.lastCol - bounds.right)
      emitMotion(KSId::l, {pos.line, pos.col + 1}, onStatic);

    if (pos.col > bounds.left)
      emitMotion(KSId::Zero, {pos.line, bounds.left}, onStatic);

    int fnb = firstNonBlankCol(pos.line);
    if (fnb >= bounds.left && fnb <= bounds.lastCol - bounds.right && fnb != pos.col)
      emitMotion(KSId::Caret, {pos.line, fnb}, onStatic);

    int dollarCol = bounds.lastCol - bounds.right;
    if (dollarCol > pos.col && dollarCol >= bounds.left)
      emitMotion(KSId::Dollar, {pos.line, dollarCol, TARGETCOL_EOL}, onStatic);
  }

  template<class OnStatic>
  void exploreVerticalMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    int lastLine = maxLineIndex();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      emitMotion(KSId::j, {newLine, newCol, pos.targetCol}, onStatic);
    }

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      emitMotion(KSId::k, {newLine, newCol, pos.targetCol}, onStatic);
    }
  }

  template<bool Forward, EdgeType Edge, class OnStatic>
  void exploreWordMotions(const std::vector<Motion::WordMotionSpecNoEdge>& specs,
                          const MotionState& base,
                          OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    auto dirBoundary = boundaryForDirection(Forward);

    for (const auto& spec : specs) {
      CursorPos endpoint = VimCore::motionWordEndpoint<Forward, Edge>(
        pos, lines_, spec.big, spec.skipCurrent,
        dirBoundary.edgeOffset, dirBoundary.hasLinesOutside, false);

      if (endpoint != POSITION_OUTSIDE_BOUNDARY) {
        emitMotion(spec.ksId, endpoint, onStatic);
      }
    }
  }

  template<bool Forward, class OnStatic>
  void exploreParagraphMotions(const std::vector<Motion::ParagraphMotionSpecNoDir>& specs,
                               const MotionState& base,
                               OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    auto dirBoundary = boundaryForDirection(Forward);
    int endpointLine = VimCore::motionParagraphEndpoint<Forward, LineEdgeType::NextEdge>(
        pos.line, lines_, dirBoundary.hasLinesOutside);

    if (endpointLine == VimCore::LINE_OUTSIDE_BOUNDARY) return;

    int endpointCol = 0;
    if constexpr (Forward) {
      int lastLine = maxLineIndex();
      if (endpointLine == lastLine && !VimCore::isBlankLineStr(lines_[endpointLine])) {
        endpointCol = horizontalBoundsForLine(endpointLine).lastCol;
      }
    }

    CursorPos endpoint(endpointLine, endpointCol);
    for (const auto& spec : specs) {
      emitMotion(spec.ksId, endpoint, onStatic);
    }
  }

  template<bool Forward, class OnStatic>
  void exploreSentenceMotions(const std::vector<Motion::SentenceMotionSpecNoDir>& specs,
                              const MotionState& base,
                              OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    auto dirBoundary = boundaryForDirection(Forward);
    CursorPos endpoint = VimCore::motionSentenceEndpoint<Forward, SentenceEdgeType::NextEdge>(
        pos, lines_, dirBoundary.edgeOffset, dirBoundary.hasLinesOutside);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY) return;

    for (const auto& spec : specs) {
      emitMotion(spec.ksId, endpoint, onStatic);
    }
  }

  template<bool Forward, class OnStatic>
  void exploreScrollMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    const auto& specs = Forward ? Motion::FORWARD_SCROLL_MOTIONS : Motion::BACKWARD_SCROLL_MOTIONS;
    for (const auto& spec : specs) {
      int shift = scrollShift(spec.isHalf, Forward);

      int targetLine = VimCore::scrollEndpoint(
          pos.line, maxLineIndex() + 1, shift,
          hasLinesAboveBoundary(), hasLinesBelowBoundary());

      if (targetLine != VimCore::LINE_OUTSIDE_BOUNDARY) {
        int endpointCol = VimOptions::startOfLine()
            ? firstNonBlankCol(targetLine)
            : clampTargetColToLine(pos.targetCol, targetLine);
        CursorPos endpoint(targetLine, endpointCol, pos.targetCol);
        emitMotion(spec.ksId, endpoint, onStatic);
      }
    }
  }

  template<class OnStatic>
  void exploreScrollMotions(const MotionState& base, OnStatic& onStatic) {
    exploreScrollMotions<true>(base, onStatic);
    exploreScrollMotions<false>(base, onStatic);
  }

  template<class OnStatic>
  void exploreJumpMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    if (!hasLinesAboveBoundary()) {
      int endpointCol = VimOptions::startOfLine()
          ? firstNonBlankCol(0)
          : clampTargetColToLine(pos.targetCol, 0);
      emitMotion(KSId::gg, {0, endpointCol, pos.targetCol}, onStatic);
    }
    if (!hasLinesBelowBoundary()) {
      int lastLine = maxLineIndex();
      int endpointCol = VimOptions::startOfLine()
          ? firstNonBlankCol(lastLine)
          : clampTargetColToLine(pos.targetCol, lastLine);
      emitMotion(KSId::G, {lastLine, endpointCol, pos.targetCol}, onStatic);
    }
  }

  template<bool Forward, class OnFMotion>
  void exploreFMotions(const MotionState& base, OnFMotion& onFMotion) {
    CursorPos pos = base.getPos();
    if (!canExploreFMotionsFrom(pos)) return;

    constexpr char firstMotion = Forward ? 'f' : 'F';
    constexpr char repeatMotion = ';';

    int goalCol = goalColForDirection(Forward);
    auto infos = VimCore::generateFMotions<Forward>(
        pos.col, goalCol, lines_[pos.line], fMotionThreshold());

    for (const auto& [c, col, cnt] : infos) {
      auto charIt = CHAR_TO_KEYS.find(c);
      if (charIt == CHAR_TO_KEYS.end()) continue;

      KeyedSequence fMotion;
      fMotion.append(firstMotion);
      fMotion.append(c);
      fMotion.append(repeatMotion, cnt);

      onFMotion(fMotion, col);
    }
  }

  template<bool Forward, LandingType LT, CountClass C, KSId ForwardKS, KSId BackwardKS,
           class OnCounted>
  void exploreCountedSpec(const MotionState& base, OnCounted& onCounted) {
    CursorPos pos = base.getPos();
    constexpr KSId motionId = Forward ? ForwardKS : BackwardKS;

    int off = bufferLineOffset_;

    Pos globalPos(pos.line + off, pos.col);
    Pos globalRangeFirst(goalRange_.first.line + off, goalRange_.first.col);
    Pos globalRangeLast(goalRange_.last.line + off, goalRange_.last.col);

    if constexpr (Forward) {
      if (!(globalPos < globalRangeFirst)) return;
    } else {
      if (!(globalPos > globalRangeLast)) return;
    }

    auto results = bufferIndex_.getClosestInRange<Forward>(
        LT, globalPos, globalRangeFirst, globalRangeLast);
    for (const auto& r : results) {
      if (!r.valid()) continue;
      if (!isValidPrefixCount(r.count)) continue;

      CursorPos localPos(r.pos.line - off, r.pos.col);
      if (!isValidLocalLandingLine(localPos.line)) continue;
      exploreCountMotion<C>(motionId, r.count, localPos, onCounted);
    }
  }

  template<bool Forward, class OnCounted>
  void exploreLineCountMotions(const MotionState& base, OnCounted& onCounted) {
    exploreCountedSpec<Forward, LandingType::WordBegin, CountClass::MotionWord,
                       KSId::w, KSId::b>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::WordEnd, CountClass::MotionWord,
                       KSId::e, KSId::ge>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::WORDBegin, CountClass::MotionWORD,
                       KSId::W, KSId::B>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::WORDEnd, CountClass::MotionWORD,
                       KSId::E, KSId::gE>(base, onCounted);
  }

  template<bool Forward, class OnCounted>
  void exploreGlobalCountMotions(const MotionState& base, OnCounted& onCounted) {
    exploreCountedSpec<Forward, LandingType::Paragraph, CountClass::MotionParagraph,
                       KSId::RBrace, KSId::LBrace>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::Sentence, CountClass::MotionSentence,
                       KSId::RParen, KSId::LParen>(base, onCounted);
  }

  template<bool Forward, class OnCounted>
  void exploreCountedVerticalMotions(const MotionState& base, OnCounted& onCounted) {
    CursorPos pos = base.getPos();
    int lastLine = maxLineIndex();

    constexpr int MAX_LINE_JUMP = 20;
    int linesAvailable = Forward ? (lastLine - pos.line) : pos.line;
    int maxCount = std::min(MAX_LINE_JUMP, linesAvailable);
    auto countRange = boundedPrefixRange(maxCount);
    if (!countRange) return;

    const KSId motion = Forward ? KSId::j : KSId::k;
    for (int cnt = countRange->min; cnt <= countRange->max; cnt++) {
      int newLine = Forward ? (pos.line + cnt) : (pos.line - cnt);
      if (!canLandOnLine(newLine)) continue;

      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      exploreCountMotion<CountClass::MotionLine>(motion, cnt,
                                                 {newLine, newCol, pos.targetCol}, onCounted);
    }
  }

  template<bool Forward, class OnCounted>
  void exploreCountedHorizontalMotions(const MotionState& base, OnCounted& onCounted) {
    CursorPos pos = base.getPos();
    auto bounds = horizontalBoundsForLine(pos.line);

    int maxCount = Forward
        ? (bounds.lastCol - bounds.right - pos.col)
        : (pos.col - bounds.left);
    auto countRange = boundedPrefixRange(maxCount);
    if (!countRange) return;

    const KSId motion = Forward ? KSId::l : KSId::h;
    for (int cnt = countRange->min; cnt <= countRange->max; cnt++) {
      int newCol = Forward ? (pos.col + cnt) : (pos.col - cnt);
      exploreCountMotion<CountClass::MotionChar>(motion, cnt,
                                                 {pos.line, newCol}, onCounted);
    }
  }

  template<bool Forward, class OnCounted, class OnFMotion>
  void exploreCountedMotionsForDirection(const MotionState& base,
                                         bool canUseLineLocalMotions,
                                         OnCounted& onCounted,
                                         OnFMotion& onFMotion) {
    if (canUseLineLocalMotions) {
      exploreFMotions<Forward>(base, onFMotion);
      exploreLineCountMotions<Forward>(base, onCounted);
      exploreCountedHorizontalMotions<Forward>(base, onCounted);
    }
    exploreGlobalCountMotions<Forward>(base, onCounted);
    exploreCountedVerticalMotions<Forward>(base, onCounted);
  }

  template<class OnStatic>
  void exploreClasses(const MotionState& base, MotionClassMask m, OnStatic& onStatic) {
    using M = MotionClassMask;
    if (has(m, M::Left))          exploreLeftMotions(base, onStatic);
    if (has(m, M::Right))         exploreRightMotions(base, onStatic);
    if (has(m, M::Up))            exploreUpMotions(base, onStatic);
    if (has(m, M::Down))          exploreDownMotions(base, onStatic);
    if (has(m, M::ForwardCross))  exploreForwardCrossingMotions(base, onStatic);
    if (has(m, M::BackwardCross)) exploreBackwardCrossingMotions(base, onStatic);
  }

  template<class OnStatic>
  void exploreLeftMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    auto bounds = horizontalBoundsForLine(pos.line);

    if (pos.col > bounds.left)
      emitMotion(KSId::h, {pos.line, pos.col - 1}, onStatic);

    if (pos.col > bounds.left)
      emitMotion(KSId::Zero, {pos.line, bounds.left}, onStatic);

    int fnb = firstNonBlankCol(pos.line);
    if (fnb >= bounds.left && fnb <= bounds.lastCol - bounds.right && fnb < pos.col)
      emitMotion(KSId::Caret, {pos.line, fnb}, onStatic);
  }

  template<class OnStatic>
  void exploreRightMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    auto bounds = horizontalBoundsForLine(pos.line);

    if (pos.col < bounds.lastCol - bounds.right)
      emitMotion(KSId::l, {pos.line, pos.col + 1}, onStatic);

    int dollarCol = bounds.lastCol - bounds.right;
    if (dollarCol > pos.col && dollarCol >= bounds.left)
      emitMotion(KSId::Dollar, {pos.line, dollarCol, TARGETCOL_EOL}, onStatic);

    int fnb = firstNonBlankCol(pos.line);
    if (fnb >= bounds.left && fnb <= bounds.lastCol - bounds.right && fnb > pos.col)
      emitMotion(KSId::Caret, {pos.line, fnb}, onStatic);
  }

  template<class OnStatic>
  void exploreUpMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      emitMotion(KSId::k, {newLine, newCol, pos.targetCol}, onStatic);
    }

    exploreScrollMotions<false>(base, onStatic);

    if (!hasLinesAboveBoundary()) {
      int endpointCol = VimOptions::startOfLine()
          ? firstNonBlankCol(0)
          : clampTargetColToLine(pos.targetCol, 0);
      emitMotion(KSId::gg, {0, endpointCol, pos.targetCol}, onStatic);
    }
  }

  template<class OnStatic>
  void exploreDownMotions(const MotionState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    int lastLine = maxLineIndex();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      emitMotion(KSId::j, {newLine, newCol, pos.targetCol}, onStatic);
    }

    exploreScrollMotions<true>(base, onStatic);

    if (!hasLinesBelowBoundary()) {
      int endpointCol = VimOptions::startOfLine()
          ? firstNonBlankCol(lastLine)
          : clampTargetColToLine(pos.targetCol, lastLine);
      emitMotion(KSId::G, {lastLine, endpointCol, pos.targetCol}, onStatic);
    }
  }

  template<class OnStatic>
  void exploreForwardCrossingMotions(const MotionState& base, OnStatic& onStatic) {
    exploreWordMotions<true, EdgeType::NextEdge>(Motion::FORWARD_NEXTEDGE_MOTIONS, base, onStatic);
    exploreWordMotions<true, EdgeType::WordEdge>(Motion::FORWARD_WORDEDGE_MOTIONS, base, onStatic);
    exploreParagraphMotions<true>(Motion::FORWARD_PARAGRAPH_MOTIONS, base, onStatic);
    exploreSentenceMotions<true>(Motion::FORWARD_SENTENCE_MOTIONS, base, onStatic);
  }

  template<class OnStatic>
  void exploreBackwardCrossingMotions(const MotionState& base, OnStatic& onStatic) {
    exploreWordMotions<false, EdgeType::WordEdge>(Motion::BACKWARD_WORDEDGE_MOTIONS, base, onStatic);
    exploreWordMotions<false, EdgeType::NextEdge>(Motion::BACKWARD_NEXTEDGE_MOTIONS, base, onStatic);
    exploreParagraphMotions<false>(Motion::BACKWARD_PARAGRAPH_MOTIONS, base, onStatic);
    exploreSentenceMotions<false>(Motion::BACKWARD_SENTENCE_MOTIONS, base, onStatic);
  }
};
