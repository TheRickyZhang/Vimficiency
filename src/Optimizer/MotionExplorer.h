#pragma once

#include "MotionSearchContext.h"
#include "MotionToSpec.h"
#include "BufferIndex.h"
#include "Keyboard/KeyboardModel.h"
#include "Keyboard/MotionToKeys.h"
#include "VimCore/VimCore.h"
#include "VimCore/VimMotionUtils.h"
#include "VimCore/VimEndpointUtils.h"

// MotionExplorer encapsulates motion exploration logic for the optimizer.
// Separates exploration methods from the main optimize() function for clarity.
//
// Two usage modes:
// 1. Single-goal (optimize): Uses goalPos for directional optimization
// 2. Range-goal (optimizeToRange): Uses exploreAllMotions only, no directional methods
class MotionExplorer {
  MotionSearchContext& ctx;
  BufferIndex* bufferIndex_ = nullptr;           // Optional: only for count motions
  const MotionToKeys* motionToKeys_ = nullptr;   // Optional: only for count motions
  Position goalPos_;                             // For single-goal mode
  PosKey goalKey_{0, 0};

public:
  // Full constructor for optimize() with directional exploration
  MotionExplorer(MotionSearchContext& ctx, const Position& goalPos,
                 BufferIndex& bufferIndex, const MotionToKeys& motionToKeys)
      : ctx(ctx), bufferIndex_(&bufferIndex), motionToKeys_(&motionToKeys),
        goalPos_(goalPos), goalKey_(goalPos.line, goalPos.col) {}

  // Lightweight constructor for optimizeToRange() - no directional optimization
  explicit MotionExplorer(MotionSearchContext& ctx)
      : ctx(ctx) {}

  // Core emit helper - creates new state, applies motion, and queues for exploration
  void emitMotion(const MotionState& base, const char* cmd,
                  Position endpoint, const PhysicalKeys& keys) {
    MotionState newState = base;
    newState.applyMotion(cmd, endpoint, keys, ctx.config);
    newState.updateCost(ctx.computePriorityToGoal(newState, goalPos_));
    ctx.exploreNewState(std::move(newState), goalKey_);
  }

  // Simple line motions: h, l, 0, ^, $
  void exploreHorizontalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lineLen = ctx.lines[pos.line].size();
    int lastCol = lineLen > 0 ? lineLen - 1 : 0;

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == ctx.lines.lastLine()) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col > leftBound)
      emitMotion(base, "h", {pos.line, pos.col - 1}, {Key::Key_H});

    if (pos.col < lastCol - rightBound)
      emitMotion(base, "l", {pos.line, pos.col + 1}, {Key::Key_L});

    if (pos.col > leftBound)
      emitMotion(base, "0", {pos.line, leftBound}, {Key::Key_0});

    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb != pos.col)
      emitMotion(base, "^", {pos.line, fnb}, {Key::Key_Shift, Key::Key_6});

    int dollarCol = lastCol - rightBound;
    if (dollarCol > pos.col && dollarCol >= leftBound)
      emitMotion(base, "$", {pos.line, dollarCol}, {Key::Key_Shift, Key::Key_4});
  }

  // Vertical motions: j, k
  void exploreVerticalMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, "j", {newLine, newCol, pos.targetCol}, {Key::Key_J});
    }

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, "k", {newLine, newCol, pos.targetCol}, {Key::Key_K});
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
        emitMotion(base, spec.cmd, endpoint, spec.keys);
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
      emitMotion(base, spec.cmd, endpoint, spec.keys);
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
      emitMotion(base, spec.cmd, endpoint, spec.keys);
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
        int endpointCol = VimCore::clampCol(ctx.lines, pos.targetCol, targetLine);
        Position endpoint(targetLine, endpointCol, pos.targetCol);
        emitMotion(base, spec.cmd, endpoint, spec.keys);
      }
    }
  }

  // Non-templated wrapper for exploreAllStandardMotions
  void exploreScrollMotions(const MotionState& base) {
    exploreScrollMotions<true>(base);
    exploreScrollMotions<false>(base);
  }

  void exploreJumpMotions(const MotionState& base) {
    if (!ctx.boundary.hasLinesAbove()) {
      int endpointCol = VimCore::firstNonBlankColInLineStr(ctx.lines[0]);
      emitMotion(base, "gg", {0, endpointCol}, {Key::Key_G, Key::Key_G});
    }
    if (!ctx.boundary.hasLinesBelow()) {
      int lastLine = ctx.lines.lastLine();
      int endpointCol = VimCore::firstNonBlankColInLineStr(ctx.lines[lastLine]);
      emitMotion(base, "G", {lastLine, endpointCol}, {Key::Key_Shift, Key::Key_G});
    }
  }

  // Counted motions with pre-computed endpoint
  void exploreCountMotion(const MotionState& base, const std::string& motion, int cnt,
                          const Position& newPos, const MotionToKeys& motionToKeys) {
    auto keyIt = motionToKeys.find(motion);
    if (keyIt == motionToKeys.end()) return;

    MotionState newState = base;
    newState.applyCountedMotion(motion, cnt, newPos, keyIt->second, ctx.config);
    newState.updateCost(ctx.computePriorityToGoal(newState, goalPos_));
    ctx.exploreNewState(std::move(newState), goalKey_);
  }

  // F-motions with known column (internal helper)
  void exploreFMotion(const MotionState& base, const std::string& motion,
                      int newcol, const PhysicalKeys& keys) {
    MotionState newState = base;
    newState.applyFMotion(motion, newcol, keys, ctx.config);
    newState.updateCost(ctx.computePriorityToGoal(newState, goalPos_));
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

      std::string motion;
      PhysicalKeys keys;
      motion += firstMotion;               keys.append(CHAR_TO_KEYS.at(firstMotion));
      motion += c;                         keys.append(charIt->second);
      motion += std::string(cnt, repeatMotion);  keys.append(CHAR_TO_KEYS.at(repeatMotion), cnt);

      exploreFMotion(base, motion, col, keys);
    }
  }

  // Count motion exploration - templated implementation
  template<bool Forward>
  void exploreCountMotionsImpl(const MotionState& base,
                               const std::vector<CountableMotionPair>& motionPairs) {
    assert(bufferIndex_ && motionToKeys_ && "Count motions require goal-based constructor");
    Position pos = base.getPos();

    for (const auto& pair : motionPairs) {
      const std::string& motion = Forward ? pair.forward : pair.backward;
      auto results = bufferIndex_->getTwoClosest(pair.type, pos, goalPos_);

      for (const auto& r : results) {
        if (!r.valid()) continue;
        if (ctx.boundary.hasLinesAbove() && r.pos.line == 0) continue;
        if (ctx.boundary.hasLinesBelow() && r.pos.line == ctx.lines.lastLine()) continue;
        exploreCountMotion(base, motion, r.count, r.pos, *motionToKeys_);
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
  // 6-Class Direction-based exploration (single-goal mode only)
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

  void exploreDirectionalStandardMotions(const MotionState& base) {
    Position pos = base.getPos();

    // Use flags to avoid duplicate exploration
    bool wantLeft = false, wantRight = false;
    bool wantUp = false, wantDown = false;
    bool wantForward = false, wantBackward = false;

    if (pos.line == goalPos_.line) {
      // Same line: only horizontal + crossing in goal direction
      if (pos.col > goalPos_.col) {
        wantLeft = wantBackward = true;
      } else {
        wantRight = wantForward = true;
      }
    } else if(pos.col == goalPos_.col) {
      if(pos.line < goalPos_.line) {
        wantDown = wantForward = true;
      } else {
        wantUp = wantBackward = true;
      }
    }
    else {
      if (pos.line < goalPos_.line) {
        wantDown = wantForward = true;
      } else {
        wantUp = wantBackward = true;
      }

      if (pos.col > goalPos_.col) {
        wantLeft = wantBackward = true;
      } else {
        wantRight = wantForward = true;
      }
    }

    // Call each class at most once
    if (wantLeft) exploreLeftMotions(base);
    if (wantRight) exploreRightMotions(base);
    if (wantUp) exploreUpMotions(base);
    if (wantDown) exploreDownMotions(base);
    if (wantForward) exploreForwardCrossingMotions(base);
    if (wantBackward) exploreBackwardCrossingMotions(base);
  }

  // --- Left: h, 0, ^ (when fnb < pos.col) ---
  void exploreLeftMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastCol = ctx.lines[pos.line].lastCol();
    int lastLine = ctx.lines.lastLine();

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == lastLine) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col > leftBound)
      emitMotion(base, "h", {pos.line, pos.col - 1}, {Key::Key_H});

    if (pos.col > leftBound)
      emitMotion(base, "0", {pos.line, leftBound}, {Key::Key_0});

    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb < pos.col)
      emitMotion(base, "^", {pos.line, fnb}, {Key::Key_Shift, Key::Key_6});
  }

  // --- Right: l, $, ^ (when fnb > pos.col) ---
  void exploreRightMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastCol = ctx.lines[pos.line].lastCol();
    int lastLine = ctx.lines.lastLine();

    int leftBound = (pos.line == 0) ? ctx.boundary.leftColOffset() : 0;
    int rightBound = (pos.line == lastLine) ? ctx.boundary.rightColOffset() : 0;

    if (pos.col < lastCol - rightBound)
      emitMotion(base, "l", {pos.line, pos.col + 1}, {Key::Key_L});

    int dollarCol = lastCol - rightBound;
    if (dollarCol > pos.col && dollarCol >= leftBound)
      emitMotion(base, "$", {pos.line, dollarCol}, {Key::Key_Shift, Key::Key_4});

    // ^ can move right if cursor is before first non-blank
    int fnb = VimCore::firstNonBlankColInLineStr(ctx.lines[pos.line]);
    if (fnb >= leftBound && fnb <= lastCol - rightBound && fnb > pos.col)
      emitMotion(base, "^", {pos.line, fnb}, {Key::Key_Shift, Key::Key_6});
  }

  // --- Up: k, <C-u>, gg ---
  void exploreUpMotions(const MotionState& base) {
    Position pos = base.getPos();

    if (pos.line > 0) {
      int newLine = pos.line - 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, "k", {newLine, newCol, pos.targetCol}, {Key::Key_K});
    }

    exploreScrollMotions<false>(base);

    if (!ctx.boundary.hasLinesAbove()) {
      int endpointCol = VimCore::firstNonBlankColInLineStr(ctx.lines[0]);
      emitMotion(base, "gg", {0, endpointCol}, {Key::Key_G, Key::Key_G});
    }
  }

  // --- Down: j, <C-d>, G ---
  void exploreDownMotions(const MotionState& base) {
    Position pos = base.getPos();
    int lastLine = ctx.lines.lastLine();

    if (pos.line < lastLine) {
      int newLine = pos.line + 1;
      int newCol = VimCore::clampCol(ctx.lines, pos.targetCol, newLine);
      emitMotion(base, "j", {newLine, newCol, pos.targetCol}, {Key::Key_J});
    }

    exploreScrollMotions<true>(base);

    if (!ctx.boundary.hasLinesBelow()) {
      int endpointCol = VimCore::firstNonBlankColInLineStr(ctx.lines[lastLine]);
      emitMotion(base, "G", {lastLine, endpointCol}, {Key::Key_Shift, Key::Key_G});
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
};
