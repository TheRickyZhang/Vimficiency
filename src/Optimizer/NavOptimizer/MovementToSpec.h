#pragma once

#include <vector>
#include "Keyboard/KeyedSequence.h"
#include "VimCore/VimEndpointUtils.h"

// =============================================================================
// Motion Operation Specs - tables for NavOptimizer
// =============================================================================
// Structured specs with operation parameters alongside pre-computed keys.
// Eliminates runtime map lookups during A* search.

namespace Movement {

// Word motions (w, b, e, ge, W, B, E, gE)
struct WordMovementSpec {
  KSId ksId;
  VimCore::WordMotionTarget target;
  bool big;
};
extern const std::vector<WordMovementSpec> WORD_MOTIONS;
extern const std::vector<WordMovementSpec> FORWARD_WORD_MOVEMENTS;
extern const std::vector<WordMovementSpec> BACKWARD_WORD_MOVEMENTS;

// Paragraph motions ({, })
struct ParagraphMovementSpec {
  KSId ksId;
  bool forward;
};
extern const std::vector<ParagraphMovementSpec> PARAGRAPH_MOTIONS;

// Split by direction for paragraph dispatch.
struct ParagraphMovementSpecNoDir {
  KSId ksId;
};
extern const std::vector<ParagraphMovementSpecNoDir> FORWARD_PARAGRAPH_MOVEMENTS;   // }
extern const std::vector<ParagraphMovementSpecNoDir> BACKWARD_PARAGRAPH_MOVEMENTS;  // {

// Sentence motions ((, ))
struct SentenceMovementSpec {
  KSId ksId;
  bool forward;
};
extern const std::vector<SentenceMovementSpec> SENTENCE_MOTIONS;

// Split by direction for sentence dispatch.
struct SentenceMovementSpecNoDir {
  KSId ksId;
};
extern const std::vector<SentenceMovementSpecNoDir> FORWARD_SENTENCE_MOVEMENTS;   // )
extern const std::vector<SentenceMovementSpecNoDir> BACKWARD_SENTENCE_MOVEMENTS;  // (

// Simple line motions (h, l, 0, ^, $) - same-line only, no boundary checks
struct SimpleLineMovementSpec {
  KSId ksId;
};
extern const std::vector<SimpleLineMovementSpec> SIMPLE_LINE_MOTIONS;

// Vertical motions (j, k)
struct VerticalMovementSpec {
  KSId ksId;
  bool isDown;
};
extern const std::vector<VerticalMovementSpec> VERTICAL_MOTIONS;

// Jump motions (gg, G)
struct JumpMovementSpec {
  KSId ksId;
  bool toStart;  // gg=true, G=false
};
extern const std::vector<JumpMovementSpec> JUMP_MOTIONS;

// Scroll motions (<C-d>, <C-u>)
// NOTE: <C-f> and <C-b> are excluded - see MovementToSpec.cpp for details
struct ScrollMovementSpec {
  KSId ksId;
  int shiftMultiplier;  // +1 for down, -1 for up
  bool isHalf;          // true for C-d/C-u (always true now, kept for future extensibility)
};
extern const std::vector<ScrollMovementSpec> SCROLL_MOTIONS;

// Split by Forward for templated dispatch (direction implicit from vector name)
struct ScrollMovementSpecNoDir {
  KSId ksId;
  bool isHalf;
};
extern const std::vector<ScrollMovementSpecNoDir> FORWARD_SCROLL_MOVEMENTS;   // <C-d>
extern const std::vector<ScrollMovementSpecNoDir> BACKWARD_SCROLL_MOVEMENTS;  // <C-u>

} // namespace Movement
