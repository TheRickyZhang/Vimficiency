#pragma once

#include <algorithm>
#include <cassert>
#include <optional>
#include <utility>

#include "Boundary/NavBoundary.h"
#include "BufferIndex.h"
#include "NavClassMask.h"
#include "NavOptimizerParams.h"
#include "MovementToSpec.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Optimizer/NavOptimizer/NavState.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Types/CharInterval.h"
#include "Types/NavContext.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimOptions.h"

// NavExplorer encapsulates motion candidate generation for A* search.
// It owns only read-only optimization inputs and streams transition intents
// to caller-provided callbacks.
class NavExplorer {
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
  NavExplorer(const Lines& lines,
                 const NavContext& navContext,
                 const NavBoundary& boundary,
                 const NavOptimizerParams& params,
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
  void exploreAllStandardMotions(const NavState& base, OnStatic&& onStatic) {
    exploreHorizontalMotions(base, onStatic);
    exploreVerticalMovements(base, onStatic);
    exploreWordMovements<true, EdgeType::NextEdge>(Movement::FORWARD_NEXTEDGE_MOVEMENTS, base, onStatic);
    exploreWordMovements<true, EdgeType::WordEdge>(Movement::FORWARD_WORDEDGE_MOVEMENTS, base, onStatic);
    exploreWordMovements<false, EdgeType::WordEdge>(Movement::BACKWARD_WORDEDGE_MOVEMENTS, base, onStatic);
    exploreWordMovements<false, EdgeType::NextEdge>(Movement::BACKWARD_NEXTEDGE_MOVEMENTS, base, onStatic);
    exploreParagraphMovements<true>(Movement::FORWARD_PARAGRAPH_MOVEMENTS, base, onStatic);
    exploreParagraphMovements<false>(Movement::BACKWARD_PARAGRAPH_MOVEMENTS, base, onStatic);
    exploreSentenceMovements<true>(Movement::FORWARD_SENTENCE_MOVEMENTS, base, onStatic);
    exploreSentenceMovements<false>(Movement::BACKWARD_SENTENCE_MOVEMENTS, base, onStatic);
    exploreScrollMovements(base, onStatic);
    exploreJumpMotions(base, onStatic);
  }

  template<class OnStatic>
  void exploreDirectionalStandardMotions(const NavState& base, OnStatic&& onStatic) {
    CursorPos pos = base.getPos();
    NavClassMask m = classesForRange(pos, goalRange_.first, goalRange_.last);
    exploreClasses(base, m, onStatic);
  }

  template<class OnCounted, class OnFMotion>
  void exploreCountedMotions(const NavState& base,
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
  const NavBoundary& boundary_;
  const NavOptimizerParams& params_;
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

  bool isValidLocalLandingPosition(const CursorPos& pos) const {
    if (!lines_.contains(pos)) return false;
    return boundary_.isPositionInBounds(
        pos, lines_.lastLine(), static_cast<int>(lines_.back().size()));
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
    assert(isValidLocalLandingPosition(newPos) &&
           "counted motion producer emitted invalid landing position");
    CountPenaltyInput in;
    in.count = cnt;
    in.span = cnt;
    double penalty = runtimeCountPenalty<C>(in);
    onCounted(baseMotion, KeyedSequence::byId(baseMotion), cnt, newPos, penalty);
  }

  template<class OnStatic>
  void exploreHorizontalMotions(const NavState& base, OnStatic& onStatic) {
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
  void exploreVerticalMovements(const NavState& base, OnStatic& onStatic) {
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
  void exploreWordMovements(const std::vector<Movement::WordMovementSpecNoEdge>& specs,
                          const NavState& base,
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
  void exploreParagraphMovements(const std::vector<Movement::ParagraphMovementSpecNoDir>& specs,
                               const NavState& base,
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
  void exploreSentenceMovements(const std::vector<Movement::SentenceMovementSpecNoDir>& specs,
                              const NavState& base,
                              OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    auto dirBoundary = boundaryForDirection(Forward);
    CursorPos endpoint = VimCore::motionSentenceEndpoint<Forward, SentenceEdgeType::NextEdge>(
        pos, lines_, dirBoundary.edgeOffset, dirBoundary.hasLinesOutside);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY) return;
    // motionSentenceEndpoint returns `pos` when there is no next/prev
    // sentence to land on. Don't surface a no-op `(` / `)`.
    if (endpoint.pos() == pos.pos()) return;

    for (const auto& spec : specs) {
      emitMotion(spec.ksId, endpoint, onStatic);
    }
  }

  template<bool Forward, class OnStatic>
  void exploreScrollMovements(const NavState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    const auto& specs = Forward ? Movement::FORWARD_SCROLL_MOVEMENTS : Movement::BACKWARD_SCROLL_MOVEMENTS;
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
  void exploreScrollMovements(const NavState& base, OnStatic& onStatic) {
    exploreScrollMovements<true>(base, onStatic);
    exploreScrollMovements<false>(base, onStatic);
  }

  template<class OnStatic>
  void exploreJumpMotions(const NavState& base, OnStatic& onStatic) {
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
  void exploreFMotions(const NavState& base, OnFMotion& onFMotion) {
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
  void exploreCountedSpec(const NavState& base, OnCounted& onCounted) {
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

    auto isAllowedEndpoint = [&](Pos indexedPos) {
      CursorPos localPos(indexedPos.line - off, indexedPos.col);
      return isValidLocalLandingPosition(localPos);
    };
    auto results = bufferIndex_.getClosestInRange<Forward>(
        LT, globalPos, globalRangeFirst, globalRangeLast, isAllowedEndpoint);
    for (const auto& r : results) {
      if (!r.valid()) continue;
      if (!isValidPrefixCount(r.count)) continue;

      CursorPos localPos(r.pos.line - off, r.pos.col);
      exploreCountMotion<C>(motionId, r.count, localPos, onCounted);
    }
  }

  template<bool Forward, class OnCounted>
  void exploreLineCountMotions(const NavState& base, OnCounted& onCounted) {
    exploreCountedSpec<Forward, LandingType::WordBegin, CountClass::MovementWord,
                       KSId::w, KSId::b>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::WordEnd, CountClass::MovementWord,
                       KSId::e, KSId::ge>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::WORDBegin, CountClass::MovementWORD,
                       KSId::W, KSId::B>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::WORDEnd, CountClass::MovementWORD,
                       KSId::E, KSId::gE>(base, onCounted);
  }

  template<bool Forward, class OnCounted>
  void exploreGlobalCountMotions(const NavState& base, OnCounted& onCounted) {
    exploreCountedSpec<Forward, LandingType::Paragraph, CountClass::MovementParagraph,
                       KSId::RBrace, KSId::LBrace>(base, onCounted);
    exploreCountedSpec<Forward, LandingType::Sentence, CountClass::MovementSentence,
                       KSId::RParen, KSId::LParen>(base, onCounted);
  }

  template<bool Forward, class OnCounted>
  void exploreCountedVerticalMotions(const NavState& base, OnCounted& onCounted) {
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
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      CursorPos newPos(newLine, newCol, pos.targetCol);
      if (!isValidLocalLandingPosition(newPos)) continue;
      exploreCountMotion<CountClass::MovementLine>(motion, cnt,
                                                 newPos, onCounted);
    }
  }

  template<bool Forward, class OnCounted>
  void exploreCountedHorizontalMotions(const NavState& base, OnCounted& onCounted) {
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
      CursorPos newPos(pos.line, newCol);
      if (!isValidLocalLandingPosition(newPos)) continue;
      exploreCountMotion<CountClass::MovementChar>(motion, cnt,
                                                 newPos, onCounted);
    }
  }

  template<bool Forward, class OnCounted, class OnFMotion>
  void exploreCountedMotionsForDirection(const NavState& base,
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
  void exploreClasses(const NavState& base, NavClassMask m, OnStatic& onStatic) {
    using M = NavClassMask;
    if (has(m, M::Left))          exploreLeftMotions(base, onStatic);
    if (has(m, M::Right))         exploreRightMotions(base, onStatic);
    if (has(m, M::Up))            exploreUpMotions(base, onStatic);
    if (has(m, M::Down))          exploreDownMotions(base, onStatic);
    if (has(m, M::ForwardCross))  exploreForwardCrossingMotions(base, onStatic);
    if (has(m, M::BackwardCross)) exploreBackwardCrossingMotions(base, onStatic);
  }

  template<class OnStatic>
  void exploreLeftMotions(const NavState& base, OnStatic& onStatic) {
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
  void exploreRightMotions(const NavState& base, OnStatic& onStatic) {
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
  void exploreUpMotions(const NavState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      emitMotion(KSId::k, {newLine, newCol, pos.targetCol}, onStatic);
    }

    exploreScrollMovements<false>(base, onStatic);

    if (!hasLinesAboveBoundary()) {
      int endpointCol = VimOptions::startOfLine()
          ? firstNonBlankCol(0)
          : clampTargetColToLine(pos.targetCol, 0);
      emitMotion(KSId::gg, {0, endpointCol, pos.targetCol}, onStatic);
    }
  }

  template<class OnStatic>
  void exploreDownMotions(const NavState& base, OnStatic& onStatic) {
    CursorPos pos = base.getPos();
    int lastLine = maxLineIndex();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = clampTargetColToLine(pos.targetCol, newLine);
      emitMotion(KSId::j, {newLine, newCol, pos.targetCol}, onStatic);
    }

    exploreScrollMovements<true>(base, onStatic);

    if (!hasLinesBelowBoundary()) {
      int endpointCol = VimOptions::startOfLine()
          ? firstNonBlankCol(lastLine)
          : clampTargetColToLine(pos.targetCol, lastLine);
      emitMotion(KSId::G, {lastLine, endpointCol, pos.targetCol}, onStatic);
    }
  }

  template<class OnStatic>
  void exploreForwardCrossingMotions(const NavState& base, OnStatic& onStatic) {
    exploreWordMovements<true, EdgeType::NextEdge>(Movement::FORWARD_NEXTEDGE_MOVEMENTS, base, onStatic);
    exploreWordMovements<true, EdgeType::WordEdge>(Movement::FORWARD_WORDEDGE_MOVEMENTS, base, onStatic);
    exploreParagraphMovements<true>(Movement::FORWARD_PARAGRAPH_MOVEMENTS, base, onStatic);
    exploreSentenceMovements<true>(Movement::FORWARD_SENTENCE_MOVEMENTS, base, onStatic);
  }

  template<class OnStatic>
  void exploreBackwardCrossingMotions(const NavState& base, OnStatic& onStatic) {
    exploreWordMovements<false, EdgeType::WordEdge>(Movement::BACKWARD_WORDEDGE_MOVEMENTS, base, onStatic);
    exploreWordMovements<false, EdgeType::NextEdge>(Movement::BACKWARD_NEXTEDGE_MOVEMENTS, base, onStatic);
    exploreParagraphMovements<false>(Movement::BACKWARD_PARAGRAPH_MOVEMENTS, base, onStatic);
    exploreSentenceMovements<false>(Movement::BACKWARD_SENTENCE_MOVEMENTS, base, onStatic);
  }
};
