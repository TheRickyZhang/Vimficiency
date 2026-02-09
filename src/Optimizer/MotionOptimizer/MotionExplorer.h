#pragma once

#include "MotionClassMask.h"
#include "MotionSearchContext.h"
#include "MotionToSpec.h"
#include "Optimizer/BufferIndex.h"
#include "Keyboard/KeyedSequence.h"
#include "Keyboard/MotionToKeys.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimEndpointUtils.h"
#include "VimCore/VimOptions.h"

// MotionExplorer encapsulates motion exploration logic for the optimizer.
// Separates exploration methods from the main optimize() function for clarity.
//
// Three usage modes:
// 1. Single-goal (optimize): Uses goalPos for directional optimization
// 2. Range-goal (optimizeToRange): Uses rangeFirst/rangeEnd for range-aware exploration
// 3. Lightweight: Uses exploreAllStandardMotions only, no directional methods
class MotionExplorer {
  MotionSearchContext& ctx;
  BufferIndex* bufferIndex_ = nullptr;           // Optional: only for count motions

  // Single-goal mode state
  Position goalPos_;
  PosKey goalKey_{0, 0};

  // Range mode state
  Position rangeFirst_;
  Position rangeEnd_;
  bool isRangeMode_ = false;

public:
  // Full constructor for optimize() with directional exploration
  MotionExplorer(MotionSearchContext& ctx, const Position& goalPos,
                 BufferIndex& bufferIndex)
      : ctx(ctx), bufferIndex_(&bufferIndex),
        goalPos_(goalPos), goalKey_(goalPos.line, goalPos.col) {}

  // Range mode constructor for optimizeToRange() with directional exploration
  MotionExplorer(MotionSearchContext& ctx,
                 const Position& rangeFirst,
                 const Position& rangeEnd)
      : ctx(ctx), rangeFirst_(rangeFirst), rangeEnd_(rangeEnd), isRangeMode_(true) {}

  // Lightweight constructor - no directional optimization
  explicit MotionExplorer(MotionSearchContext& ctx)
      : ctx(ctx) {}

  // Core emit helper - creates new state, applies motion, and queues for exploration.
  // Mode-aware: uses goal or range depending on constructor used.
  void emitMotion(const MotionState& base, const KeyedSequence& ks, Position endpoint) {
    MotionState newState = base.afterMotion(ks, endpoint, ctx.config);

    if (isRangeMode_) {
      newState.setCost(ctx.computePriorityToRange(newState, rangeFirst_, rangeEnd_));
      ctx.exploreNewStateToRange(std::move(newState), rangeFirst_, rangeEnd_);
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
      emitMotion(base, KeyedSequence::h, {pos.line, pos.col - 1});

    if (pos.col < lastCol - rightBound)
      emitMotion(base, KeyedSequence::l, {pos.line, pos.col + 1});

    if (pos.col > leftBound)
      emitMotion(base, KeyedSequence::Zero, {pos.line, leftBound});

    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb != pos.col)
      emitMotion(base, KeyedSequence::Caret, {pos.line, fnb});

    int dollarCol = lastCol - rightBound;
    if (dollarCol > pos.col && dollarCol >= leftBound)
      emitMotion(base, KeyedSequence::Dollar, {pos.line, dollarCol, TARGETCOL_EOL});
  }

  // Vertical motions: j, k
  void exploreVerticalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KeyedSequence::j, {newLine, newCol, pos.targetCol});
    }

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KeyedSequence::k, {newLine, newCol, pos.targetCol});
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
        emitMotion(base, ksById(spec.ksId), endpoint);
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
      emitMotion(base, ksById(spec.ksId), endpoint);
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
      emitMotion(base, ksById(spec.ksId), endpoint);
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
        emitMotion(base, ksById(spec.ksId), endpoint);
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
      emitMotion(base, KeyedSequence::gg, {0, endpointCol, pos.targetCol});
    }
    if (!ctx.boundary.hasLinesBelow()) {
      int lastLine = ctx.lines.lastLine();
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[lastLine])
          : VimCore::clampCol(ctx.lines, pos.targetCol, lastLine);
      emitMotion(base, KeyedSequence::G, {lastLine, endpointCol, pos.targetCol});
    }
  }

  // Counted motions with pre-computed endpoint
  void exploreCountMotion(const MotionState& base, const KeyedSequence& baseMotion,
                          int cnt, const Position& newPos) {
    MotionState newState = base.afterCountedMotion(baseMotion, cnt, newPos, ctx.config);
    newState.setCost(ctx.computePriorityToGoal(newState, goalPos_));
    ctx.exploreNewState(std::move(newState), goalKey_);
  }

  // F-motions with known column (internal helper)
  void exploreFMotion(const MotionState& base, const KeyedSequence& fMotion, int newcol) {
    MotionState newState = base.afterFMotion(fMotion, newcol, ctx.config);
    newState.setCost(ctx.computePriorityToGoal(newState, goalPos_));
    ctx.exploreNewState(std::move(newState), goalKey_);
  }

  // ==========================================================================
  // Templated directional exploration (requires goal-based constructor)
  // ==========================================================================

  // F-motions templated on direction - generates f{c};... or F{c};... sequences
  template<bool Forward>
  void exploreFMotions(const MotionState& base) {
    assert(bufferIndex_ && "exploreFMotions requires goal-based constructor");
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
      fMotion.appendChar(firstMotion);
      fMotion.appendChar(c);
      fMotion.appendChar(repeatMotion, cnt);

      exploreFMotion(base, fMotion, col);
    }
  }

  // Count motion exploration - templated implementation
  template<bool Forward>
  void exploreCountMotionsImpl(const MotionState& base,
                               const std::vector<CountableMotionPair>& motionPairs) {
    assert(bufferIndex_ && "Count motions require goal-based constructor");
    Position pos = base.getPos();

    for (const auto& pair : motionPairs) {
      const KeyedSequence& motion = Forward ? pair.forward : pair.backward;
      auto results = bufferIndex_->getTwoClosest(pair.type, pos, goalPos_);

      for (const auto& r : results) {
        if (!r.valid()) continue;
        if (ctx.boundary.hasLinesAbove() && r.pos.line == 0) continue;
        if (ctx.boundary.hasLinesBelow() && r.pos.line == ctx.lines.lastLine()) continue;
        exploreCountMotion(base, motion, r.count, r.pos);
      }
    }
  }

  // Line-local count motions (word motions - used when on same line as goal)
  template<bool Forward>
  void exploreLineCountMotions(const MotionState& base) {
    exploreCountMotionsImpl<Forward>(base, COUNT_SEARCHABLE_MOTIONS_LINE);
  }

  // Global count motions (paragraph/sentence motions)
  template<bool Forward>
  void exploreGlobalCountMotions(const MotionState& base) {
    exploreCountMotionsImpl<Forward>(base, COUNT_SEARCHABLE_MOTIONS_GLOBAL);
  }

  // Unified directional dispatch - call this from the main loop
  template<bool Forward>
  void exploreDirectionalMotions(const MotionState& base, bool isSameLine) {
    if (isSameLine) {
      exploreFMotions<Forward>(base);
      exploreLineCountMotions<Forward>(base);
    }
    exploreGlobalCountMotions<Forward>(base);
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
    MotionClassMask m = classesForRange(pos.line, pos.col,
                                         rangeFirst_.line, rangeFirst_.col,
                                         rangeEnd_.line, rangeEnd_.col);
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
      emitMotion(base, KeyedSequence::h, {pos.line, pos.col - 1});

    if (pos.col > leftBound)
      emitMotion(base, KeyedSequence::Zero, {pos.line, leftBound});

    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb < pos.col)
      emitMotion(base, KeyedSequence::Caret, {pos.line, fnb});
  }

  // --- Right: l, $, ^ (when fnb > pos.col) ---
  void exploreRightMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastCol = ctx.lines[pos.line].lastCol();
    int lastLine = ctx.lines.lastLine();

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == lastLine) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col < lastCol - rightBound)
      emitMotion(base, KeyedSequence::l, {pos.line, pos.col + 1});

    int dollarCol = lastCol - rightBound;
    if (dollarCol > pos.col && dollarCol >= leftBound)
      emitMotion(base, KeyedSequence::Dollar, {pos.line, dollarCol, TARGETCOL_EOL});

    // ^ can move right if cursor is before first non-blank
    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb > pos.col)
      emitMotion(base, KeyedSequence::Caret, {pos.line, fnb});
  }

  // --- Up: k, <C-u>, gg ---
  void exploreUpMotions(const MotionState& base) {
    Position pos = base.getPos();

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KeyedSequence::k, {newLine, newCol, pos.targetCol});
    }

    exploreScrollMotions<false>(base);

    if (!ctx.boundary.hasLinesAbove()) {
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[0])
          : VimCore::clampCol(ctx.lines, pos.targetCol, 0);
      emitMotion(base, KeyedSequence::gg, {0, endpointCol, pos.targetCol});
    }
  }

  // --- Down: j, <C-d>, G ---
  void exploreDownMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, KeyedSequence::j, {newLine, newCol, pos.targetCol});
    }

    exploreScrollMotions<true>(base);

    if (!ctx.boundary.hasLinesBelow()) {
      int endpointCol = VimOptions::startOfLine()
          ? VimCore::firstNonBlankColInLineStr(ctx.lines[lastLine])
          : VimCore::clampCol(ctx.lines, pos.targetCol, lastLine);
      emitMotion(base, KeyedSequence::G, {lastLine, endpointCol, pos.targetCol});
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
  const Position& getRangeFirst() const { return rangeFirst_; }
  const Position& getRangeEnd() const { return rangeEnd_; }
};
