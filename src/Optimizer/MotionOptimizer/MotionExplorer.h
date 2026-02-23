#pragma once

#include "MotionClassMask.h"
#include "MotionSearchContext.h"
#include "MotionToSpec.h"
#include "BufferIndex.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/ToKeys/MotionToKeys.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimOptions.h"

// MotionExplorer encapsulates motion exploration logic for the optimizer.
// Separates exploration methods from the main optimize() function for clarity.
//
// Three usage modes:
// 1. Single-goal (optimize): Uses goalPos for directional optimization
// 2. Range-goal (optimizeToRange): Uses rangeBegin/rangeEnd for range-aware exploration
// 3. Lightweight: Uses exploreAllStandardMotions only, no directional methods
class MotionExplorer {
  MotionSearchContext& ctx;
  BufferIndexRef bufferRef_;                     // Optional: index + offset for count motions

  // Single-goal mode state
  Position goalPos_;
  PosKey goalKey_{0, 0};

  // Range mode state
  Position rangeBegin_;
  Position rangeEnd_;
  Position rangeTail_;
  bool isRangeMode_ = false;

public:
  // Full constructor for optimize() with directional exploration
  MotionExplorer(MotionSearchContext& ctx, const Position& goalPos,
                 BufferIndex& bufferIndex)
      : ctx(ctx), bufferRef_{&bufferIndex, 0},
        goalPos_(goalPos), goalKey_(goalPos.line, goalPos.col) {}

  // Range mode constructor for optimizeToRange() with directional exploration
  MotionExplorer(MotionSearchContext& ctx,
                 const Position& rangeBegin,
                 const Position& rangeEnd,
                 const BufferIndex& bufferIndex,
                 int lineOffset)
      : ctx(ctx), bufferRef_{&bufferIndex, lineOffset},
        rangeBegin_(rangeBegin), rangeEnd_(rangeEnd), isRangeMode_(true) {
    rangeTail_ = ctx.lines.getPrevPos(rangeEnd_);
    if (rangeTail_ == POSITION_OUTSIDE_BOUNDARY) {
      rangeTail_ = rangeBegin_;
    }
  }

  // Lightweight constructor - no directional optimization
  explicit MotionExplorer(MotionSearchContext& ctx)
      : ctx(ctx) {}

  // Core emit helper - creates new state, applies motion, and queues for exploration.
  // Mode-aware: uses goal or range depending on constructor used.
  // Uses pre-computed effort from EffortBank via KSId.
  void emitMotion(const MotionState& base, KSId id, Position endpoint) {
    const KeyedSequence& ks = KeyedSequence::byId(id);
    MotionState newState = base.afterMotion(ks, ctx.bank[id], endpoint, ctx.config);

    if (isRangeMode_) {
      newState.setCost(ctx.computePriorityToRange(newState, rangeBegin_, rangeEnd_, rangeTail_));
      ctx.exploreNewStateToRange(std::move(newState), rangeBegin_, rangeEnd_);
    } else {
      newState.setCost(ctx.computePriorityToGoal(newState, goalPos_));
      ctx.exploreNewState(std::move(newState), goalKey_);
    }
  }

  // Simple line motions: h, l, 0, ^, $
  void exploreHorizontalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lineLen = ctx.lines[pos.line].size();
    int lastCol = lineLen > 0 ? lineLen - 1 : 0;

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == ctx.lines.lastLine()) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col > leftBound)
      emitMotion(base, KSId::h, {pos.line, pos.col - 1});

    if (pos.col < lastCol - rightBound)
      emitMotion(base, KSId::l, {pos.line, pos.col + 1});

    if (pos.col > leftBound)
      emitMotion(base, KSId::Zero, {pos.line, leftBound});

    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb != pos.col)
      emitMotion(base, KSId::Caret, {pos.line, fnb});

    int dollarCol = lastCol - rightBound;
    if (dollarCol > pos.col && dollarCol >= leftBound)
      emitMotion(base, KSId::Dollar, {pos.line, dollarCol, TARGETCOL_EOL});
  }

  // Vertical motions: j, k
  void exploreVerticalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KSId::j, {newLine, newCol, pos.targetCol});
    }

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KSId::k, {newLine, newCol, pos.targetCol});
    }
  }

  // Word motions - templated on direction and edge type
  template<bool Forward, EdgeType Edge>
  void exploreWordMotions(const std::vector<Motion::WordMotionSpecNoEdge>& specs,
                          const MotionState& base) {
    Position pos = base.getPos();
    int boundaryOffset = Forward ? ctx.boundary.rightColOffset() : ctx.boundary.leftColOffset();
    bool hasLinesOutside = Forward ? ctx.boundary.hasLinesBelow() : ctx.boundary.hasLinesAbove();

    for (const auto& spec : specs) {
      Position endpoint = VimCore::motionWordEndpoint<Forward, Edge>(
          pos, ctx.lines, spec.big, spec.skipCurrent,
          boundaryOffset, hasLinesOutside, false);

      if (endpoint != POSITION_OUTSIDE_BOUNDARY) {
        emitMotion(base, spec.ksId, endpoint);
      }
    }
  }

  // Paragraph motions - templated on direction
  template<bool Forward>
  void exploreParagraphMotions(const std::vector<Motion::ParagraphMotionSpecNoDir>& specs,
                               const MotionState& base) {
    Position pos = base.getPos();

    bool hasLinesOutside = Forward ? ctx.boundary.hasLinesBelow() : ctx.boundary.hasLinesAbove();
    int endpointLine = VimCore::motionParagraphEndpoint<Forward, LineEdgeType::NextEdge>(
        pos.line, ctx.lines, hasLinesOutside);

    if (endpointLine == VimCore::LINE_OUTSIDE_BOUNDARY) return;

    int endpointCol = 0;
    if constexpr (Forward) {
      int lastLine = ctx.lines.lastLine();
      if (endpointLine == lastLine && !VimCore::isBlankLineStr(ctx.lines[endpointLine])) {
        endpointCol = std::max(0, static_cast<int>(ctx.lines[endpointLine].size()) - 1);
      }
    }
    Position endpoint(endpointLine, endpointCol);

    for (const auto& spec : specs) {
      emitMotion(base, spec.ksId, endpoint);
    }
  }

  // Sentence motions - templated on direction
  template<bool Forward>
  void exploreSentenceMotions(const std::vector<Motion::SentenceMotionSpecNoDir>& specs,
                              const MotionState& base) {
    Position pos = base.getPos();

    int boundaryOffset = Forward ? ctx.boundary.rightColOffset() : ctx.boundary.leftColOffset();
    bool hasLinesOutside = Forward ? ctx.boundary.hasLinesBelow() : ctx.boundary.hasLinesAbove();
    Position endpoint = VimCore::motionSentenceEndpoint<Forward, SentenceEdgeType::NextEdge>(
        pos, ctx.lines, boundaryOffset, hasLinesOutside);

    if (endpoint == POSITION_OUTSIDE_BOUNDARY) return;

    for (const auto& spec : specs) {
      emitMotion(base, spec.ksId, endpoint);
    }
  }

  // Scroll motions - templated on direction
  template<bool Forward>
  void exploreScrollMotions(const MotionState& base) {
    Position pos = base.getPos();

    const auto& specs = Forward ? Motion::FORWARD_SCROLL_MOTIONS : Motion::BACKWARD_SCROLL_MOTIONS;
    constexpr int shiftMultiplier = Forward ? +1 : -1;

    for (const auto& spec : specs) {
      int shift = spec.isHalf
          ? shiftMultiplier * ctx.navContext.scrollAmount
          : shiftMultiplier * std::max(0, ctx.navContext.windowHeight - 2);

      int targetLine = VimCore::scrollEndpoint(
          pos.line, static_cast<int>(ctx.lines.size()), shift,
          ctx.boundary.hasLinesAbove(), ctx.boundary.hasLinesBelow());

      if (targetLine != VimCore::LINE_OUTSIDE_BOUNDARY) {
        int endpointCol = VimOptions::startOfLine()
            ? VimCore::firstNonBlankColInLineStr(ctx.lines[targetLine])
            : VimCore::clampCol(ctx.lines, pos.targetCol, targetLine);
        Position endpoint(targetLine, endpointCol, pos.targetCol);
        emitMotion(base, spec.ksId, endpoint);
      }
    }
  }

  // Non-templated wrapper for exploreAllStandardMotions
  void exploreScrollMotions(const MotionState& base) {
    exploreScrollMotions<true>(base);
    exploreScrollMotions<false>(base);
  }

  void exploreJumpMotions(const MotionState& base) {
    Position pos = base.getPos();
    if (!ctx.boundary.hasLinesAbove()) {
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[0])
          : VimCore::clampCol(ctx.lines, pos.targetCol, 0);
      emitMotion(base, KSId::gg, {0, endpointCol, pos.targetCol});
    }
    if (!ctx.boundary.hasLinesBelow()) {
      int lastLine = ctx.lines.lastLine();
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[lastLine])
          : VimCore::clampCol(ctx.lines, pos.targetCol, lastLine);
      emitMotion(base, KSId::G, {lastLine, endpointCol, pos.targetCol});
    }
  }

  template<CountClass C>
  void exploreCountMotion(const MotionState& base, const KeyedSequence& baseMotion,
                          int cnt, const Position& newPos) {
    assert(cnt >= 0 && cnt <= MAX_PREFIX_COUNT);
    CountPenaltyInput in;
    in.count = cnt;
    in.span = cnt;
    double penalty = runtimeCountPenalty<C>(in);

    MotionState newState = base.afterCountedMotion(baseMotion, cnt, newPos, ctx.config, penalty);
    if (isRangeMode_) {
      newState.setCost(ctx.computePriorityToRange(newState, rangeBegin_, rangeEnd_, rangeTail_));
      ctx.exploreNewStateToRange(std::move(newState), rangeBegin_, rangeEnd_);
    } else {
      newState.setCost(ctx.computePriorityToGoal(newState, goalPos_));
      ctx.exploreNewState(std::move(newState), goalKey_);
    }
  }

  // F-motions with known column (internal helper)
  void exploreFMotion(const MotionState& base, const KeyedSequence& fMotion, int newcol) {
    MotionState newState = base.afterFMotion(fMotion, newcol, ctx.config);
    if (isRangeMode_) {
      newState.setCost(ctx.computePriorityToRange(newState, rangeBegin_, rangeEnd_, rangeTail_));
      ctx.exploreNewStateToRange(std::move(newState), rangeBegin_, rangeEnd_);
    } else {
      newState.setCost(ctx.computePriorityToGoal(newState, goalPos_));
      ctx.exploreNewState(std::move(newState), goalKey_);
    }
  }

  // ==========================================================================
  // Templated directional exploration (requires goal-based constructor)
  // ==========================================================================

  // F-motions templated on direction - generates f{c};... or F{c};... sequences
  template<bool Forward>
  void exploreFMotions(const MotionState& base) {
    assert(bufferRef_ && "exploreFMotions requires goal-based constructor");
    Position pos = base.getPos();
    if (pos.line != goalPos_.line) return;

    constexpr char firstMotion = Forward ? 'f' : 'F';
    constexpr char repeatMotion = ';';

    auto infos = VimCore::generateFMotions<Forward>(
        pos.col, goalPos_.col, ctx.lines[pos.line], ctx.params.fMotionThreshold);

    for (const auto& [c, col, cnt] : infos) {
      auto charIt = CHAR_TO_KEYS.find(c);
      if (charIt == CHAR_TO_KEYS.end()) continue;

      KeyedSequence fMotion;
      fMotion.append(firstMotion);
      fMotion.append(c);
      fMotion.append(repeatMotion, cnt);

      exploreFMotion(base, fMotion, col);
    }
  }

  template<bool Forward, LandingType LT, CountClass C, KSId ForwardKS, KSId BackwardKS>
  void exploreCountedSpec(const MotionState& base) {
    assert(bufferRef_ && "Count motions require goal-based constructor");
    Position pos = base.getPos();
    const KeyedSequence& motion = []() -> const KeyedSequence& {
      if constexpr (Forward) return KeyedSequence::byId(ForwardKS);
      else return KeyedSequence::byId(BackwardKS);
    }();

    auto results = bufferRef_.index->getTwoClosest(LT, pos, goalPos_);

    for (const auto& r : results) {
      if (!r.valid()) continue;
      if (r.count < ctx.params.minPrefixCount) continue;
      if (r.count > ctx.params.maxPrefixCount) continue;
      if (ctx.boundary.hasLinesAbove() && r.pos.line == 0) continue;
      if (ctx.boundary.hasLinesBelow() && r.pos.line == ctx.lines.lastLine()) continue;
      exploreCountMotion<C>(base, motion, r.count, r.pos);
    }
  }

  // Line-local count motions (word motions - used when on same line as goal)
  template<bool Forward>
  void exploreLineCountMotions(const MotionState& base) {
    exploreCountedSpec<Forward, LandingType::WordBegin, CountClass::MotionWord,
                       KSId::w, KSId::b>(base);
    exploreCountedSpec<Forward, LandingType::WordEnd, CountClass::MotionWord,
                       KSId::e, KSId::ge>(base);
    exploreCountedSpec<Forward, LandingType::WORDBegin, CountClass::MotionWORD,
                       KSId::W, KSId::B>(base);
    exploreCountedSpec<Forward, LandingType::WORDEnd, CountClass::MotionWORD,
                       KSId::E, KSId::gE>(base);
  }

  // Global count motions (paragraph/sentence motions)
  template<bool Forward>
  void exploreGlobalCountMotions(const MotionState& base) {
    exploreCountedSpec<Forward, LandingType::Paragraph, CountClass::MotionParagraph,
                       KSId::RBrace, KSId::LBrace>(base);
    exploreCountedSpec<Forward, LandingType::Sentence, CountClass::MotionSentence,
                       KSId::RParen, KSId::LParen>(base);
  }

  // Counted vertical motions: {count}j or {count}k
  template<bool Forward>
  void exploreCountedVerticalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    // Cap at 20 lines; future: could be viewport-based
    constexpr int MAX_LINE_JUMP = 20;

    int linesAvailable = Forward ? (lastLine - pos.line) : pos.line;
    int maxCount = std::min(MAX_LINE_JUMP, linesAvailable);
    maxCount = std::min(maxCount, ctx.params.maxPrefixCount);

    const KeyedSequence& motion = Forward ? KeyedSequence::j : KeyedSequence::k;

    for (int cnt = ctx.params.minPrefixCount; cnt <= maxCount; cnt++) {
      int newLine = Forward ? (pos.line + cnt) : (pos.line - cnt);

      // Boundary guard: don't land on boundary lines
      if (ctx.boundary.hasLinesAbove() && newLine == 0) continue;
      if (ctx.boundary.hasLinesBelow() && newLine == lastLine) continue;

      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      exploreCountMotion<CountClass::MotionLine>(base, motion, cnt, {newLine, newCol, pos.targetCol});
    }
  }

  // Counted horizontal motions: {count}l or {count}h
  template<bool Forward>
  void exploreCountedHorizontalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastCol = ctx.lines[pos.line].lastCol();
    int lastLine = ctx.lines.lastLine();

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == lastLine) ? ctx.boundary.rightColOffset() : 0;

    int maxCount = Forward
        ? (lastCol - rightBound - pos.col)
        : (pos.col - leftBound);
    maxCount = std::min(maxCount, ctx.params.maxPrefixCount);

    const KeyedSequence& motion = Forward ? KeyedSequence::l : KeyedSequence::h;

    for (int cnt = ctx.params.minPrefixCount; cnt <= maxCount; cnt++) {
      int newCol = Forward ? (pos.col + cnt) : (pos.col - cnt);
      exploreCountMotion<CountClass::MotionChar>(base, motion, cnt, {pos.line, newCol});
    }
  }

  // Unified directional dispatch - call this from the main loop
  template<bool Forward>
  void exploreDirectionalMotions(const MotionState& base, bool isSameLine) {
    if (isSameLine) {
      exploreFMotions<Forward>(base);
      exploreLineCountMotions<Forward>(base);
      exploreCountedHorizontalMotions<Forward>(base);
    }
    exploreGlobalCountMotions<Forward>(base);
    exploreCountedVerticalMotions<Forward>(base);
  }

  // ==========================================================================
  // Range-aware counted spec exploration (uses BufferIndex with coordinate conversion)
  // ==========================================================================

  template<bool Forward, LandingType LT, CountClass C, KSId ForwardKS, KSId BackwardKS>
  void exploreCountedSpecToRange(const MotionState& base) {
    assert(bufferRef_ && "Count motions to range require BufferIndex");
    Position pos = base.getPos();
    const KeyedSequence& motion = []() -> const KeyedSequence& {
      if constexpr (Forward) return KeyedSequence::byId(ForwardKS);
      else return KeyedSequence::byId(BackwardKS);
    }();

    int off = bufferRef_.lineOffset;

    // Convert local → global coordinates for BufferIndex query
    Position globalPos(pos.line + off, pos.col, pos.targetCol);
    Position globalRangeBegin(rangeBegin_.line + off, rangeBegin_.col);
    Position globalRangeEnd(rangeEnd_.line + off, rangeEnd_.col);

    auto results = bufferRef_.index->getClosestInRange(LT, globalPos, globalRangeBegin, globalRangeEnd);
    for (const auto& r : results) {
      if (!r.valid()) continue;
      if (r.count < ctx.params.minPrefixCount) continue;
      if (r.count > ctx.params.maxPrefixCount) continue;
      // Convert global → local coordinates
      Position localPos(r.pos.line - off, r.pos.col);
      // Bounds + boundary guards: BufferIndex covers the full buffer,
      // so results can map outside the local subset.
      if (localPos.line < 0 || localPos.line > ctx.lines.lastLine()) continue;
      if (ctx.boundary.hasLinesAbove() && localPos.line == 0) continue;
      if (ctx.boundary.hasLinesBelow() && localPos.line == ctx.lines.lastLine()) continue;
      exploreCountMotion<C>(base, motion, r.count, localPos);
    }
  }

  // Line-local count motions to range (word motions)
  template<bool Forward>
  void exploreLineCountMotionsToRange(const MotionState& base) {
    exploreCountedSpecToRange<Forward, LandingType::WordBegin, CountClass::MotionWord,
                              KSId::w, KSId::b>(base);
    exploreCountedSpecToRange<Forward, LandingType::WordEnd, CountClass::MotionWord,
                              KSId::e, KSId::ge>(base);
    exploreCountedSpecToRange<Forward, LandingType::WORDBegin, CountClass::MotionWORD,
                              KSId::W, KSId::B>(base);
    exploreCountedSpecToRange<Forward, LandingType::WORDEnd, CountClass::MotionWORD,
                              KSId::E, KSId::gE>(base);
  }

  // Global count motions to range (paragraph/sentence motions)
  template<bool Forward>
  void exploreGlobalCountMotionsToRange(const MotionState& base) {
    exploreCountedSpecToRange<Forward, LandingType::Paragraph, CountClass::MotionParagraph,
                              KSId::RBrace, KSId::LBrace>(base);
    exploreCountedSpecToRange<Forward, LandingType::Sentence, CountClass::MotionSentence,
                              KSId::RParen, KSId::LParen>(base);
  }

  // Unified range directional dispatch - call from optimizeToRangeImpl loop
  template<bool Forward>
  void exploreDirectionalMotionsToRange(const MotionState& base, bool isSameLine) {
    if (isSameLine) {
      exploreLineCountMotionsToRange<Forward>(base);
      exploreCountedHorizontalMotions<Forward>(base);
    }
    exploreGlobalCountMotionsToRange<Forward>(base);
    exploreCountedVerticalMotions<Forward>(base);
  }

  // ==========================================================================
  // Non-directional exploration (works for both single-goal and range modes)
  // ==========================================================================

  void exploreAllStandardMotions(const MotionState& base) {
    exploreHorizontalMotions(base);
    exploreVerticalMotions(base);
    exploreWordMotions<true, EdgeType::NextEdge>(Motion::FORWARD_NEXTEDGE_MOTIONS, base);
    exploreWordMotions<true, EdgeType::WordEdge>(Motion::FORWARD_WORDEDGE_MOTIONS, base);
    exploreWordMotions<false, EdgeType::WordEdge>(Motion::BACKWARD_WORDEDGE_MOTIONS, base);
    exploreWordMotions<false, EdgeType::NextEdge>(Motion::BACKWARD_NEXTEDGE_MOTIONS, base);
    exploreParagraphMotions<true>(Motion::FORWARD_PARAGRAPH_MOTIONS, base);
    exploreParagraphMotions<false>(Motion::BACKWARD_PARAGRAPH_MOTIONS, base);
    exploreSentenceMotions<true>(Motion::FORWARD_SENTENCE_MOTIONS, base);
    exploreSentenceMotions<false>(Motion::BACKWARD_SENTENCE_MOTIONS, base);
    exploreScrollMotions(base);
    exploreJumpMotions(base);
  }

  // ==========================================================================
  // 6-Class Direction-based exploration
  // ==========================================================================
  // Classes:
  //   Left:             h, 0, ^
  //   Right:            l, $
  //   Up:               k, <C-u>, gg
  //   Down:             j, <C-d>, G
  //   Forward-crossing: w, W, e, E, }, )  (unpredictable horizontal when crossing lines)
  //   Backward-crossing: b, B, ge, gE, {, (
  //
  // Selection logic:
  //   Same line: Left+Backward OR Right+Forward (2/6)
  //   Different lines:
  //     - Vertical: Down+Forward OR Up+Backward based on goal.line vs pos.line
  //     - Horizontal: Left+Backward OR Right+Forward based on goal.col vs pos.col
  //   Result: 2-4 classes explored per state

  // Explore motion classes based on bitmask
  void exploreClasses(const MotionState& base, MotionClassMask m) {
    using M = MotionClassMask;
    if (has(m, M::Left))          exploreLeftMotions(base);
    if (has(m, M::Right))         exploreRightMotions(base);
    if (has(m, M::Up))            exploreUpMotions(base);
    if (has(m, M::Down))          exploreDownMotions(base);
    if (has(m, M::ForwardCross))  exploreForwardCrossingMotions(base);
    if (has(m, M::BackwardCross)) exploreBackwardCrossingMotions(base);
  }

  // Single-goal directional exploration
  void exploreDirectionalStandardMotions(const MotionState& base) {
    Position pos = base.getPos();
    MotionClassMask m = classesForSingleGoal(pos.line, pos.col, goalPos_.line, goalPos_.col);
    exploreClasses(base, m);
  }

  // Range-goal directional exploration
  void exploreDirectionalStandardMotionsToRange(const MotionState& base) {
    assert(isRangeMode_ && "exploreDirectionalStandardMotionsToRange requires range constructor");
    Position pos = base.getPos();
    MotionClassMask m = classesForRangeWithTail(
        pos.line, pos.col,
        rangeBegin_.line, rangeBegin_.col,
        rangeEnd_.line, rangeEnd_.col,
        rangeTail_.line, rangeTail_.col);
    exploreClasses(base, m);
  }

  // --- Left: h, 0, ^ (when fnb < pos.col) ---
  void exploreLeftMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastCol = ctx.lines[pos.line].lastCol();
    int lastLine = ctx.lines.lastLine();

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == lastLine) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col > leftBound)
      emitMotion(base, KSId::h, {pos.line, pos.col - 1});

    if (pos.col > leftBound)
      emitMotion(base, KSId::Zero, {pos.line, leftBound});

    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb < pos.col)
      emitMotion(base, KSId::Caret, {pos.line, fnb});
  }

  // --- Right: l, $, ^ (when fnb > pos.col) ---
  void exploreRightMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastCol = ctx.lines[pos.line].lastCol();
    int lastLine = ctx.lines.lastLine();

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == lastLine) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col < lastCol - rightBound)
      emitMotion(base, KSId::l, {pos.line, pos.col + 1});

    int dollarCol = lastCol - rightBound;
    if (dollarCol > pos.col && dollarCol >= leftBound)
      emitMotion(base, KSId::Dollar, {pos.line, dollarCol, TARGETCOL_EOL});

    // ^ can move right if cursor is before first non-blank
    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb > pos.col)
      emitMotion(base, KSId::Caret, {pos.line, fnb});
  }

  // --- Up: k, <C-u>, gg ---
  void exploreUpMotions(const MotionState& base) {
    Position pos = base.getPos();

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KSId::k, {newLine, newCol, pos.targetCol});
    }

    exploreScrollMotions<false>(base);

    if (!ctx.boundary.hasLinesAbove()) {
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[0])
          : VimCore::clampCol(ctx.lines, pos.targetCol, 0);
      emitMotion(base, KSId::gg, {0, endpointCol, pos.targetCol});
    }
  }

  // --- Down: j, <C-d>, G ---
  void exploreDownMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KSId::j, {newLine, newCol, pos.targetCol});
    }

    exploreScrollMotions<true>(base);

    if (!ctx.boundary.hasLinesBelow()) {
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[lastLine])
          : VimCore::clampCol(ctx.lines, pos.targetCol, lastLine);
      emitMotion(base, KSId::G, {lastLine, endpointCol, pos.targetCol});
    }
  }

  // --- Forward-crossing: w, W, e, E, }, ) ---
  void exploreForwardCrossingMotions(const MotionState& base) {
    exploreWordMotions<true, EdgeType::NextEdge>(Motion::FORWARD_NEXTEDGE_MOTIONS, base);
    exploreWordMotions<true, EdgeType::WordEdge>(Motion::FORWARD_WORDEDGE_MOTIONS, base);
    exploreParagraphMotions<true>(Motion::FORWARD_PARAGRAPH_MOTIONS, base);
    exploreSentenceMotions<true>(Motion::FORWARD_SENTENCE_MOTIONS, base);
  }

  // --- Backward-crossing: b, B, ge, gE, {, ( ---
  void exploreBackwardCrossingMotions(const MotionState& base) {
    exploreWordMotions<false, EdgeType::WordEdge>(Motion::BACKWARD_WORDEDGE_MOTIONS, base);
    exploreWordMotions<false, EdgeType::NextEdge>(Motion::BACKWARD_NEXTEDGE_MOTIONS, base);
    exploreParagraphMotions<false>(Motion::BACKWARD_PARAGRAPH_MOTIONS, base);
    exploreSentenceMotions<false>(Motion::BACKWARD_SENTENCE_MOTIONS, base);
  }

  const PosKey& getGoalKey() const { return goalKey_; }
  const Position& getEndPos() const { return goalPos_; }

  // Range mode accessors
  bool isRangeMode() const { return isRangeMode_; }
  const Position& getRangeBegin() const { return rangeBegin_; }
  const Position& getRangeEnd() const { return rangeEnd_; }
};
