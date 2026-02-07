#pragma once

#include <string_view>
#include <vector>
#include "VimCore/EdgeType.h"
#include "Keyboard/KeyboardModel.h"

// =============================================================================
// Motion Operation Specs - tables for MotionOptimizer
// =============================================================================
// Structured specs with operation parameters alongside pre-computed keys.
// Eliminates runtime map lookups during A* search.

namespace Motion {

// Word motions (w, b, e, ge, W, B, E, gE)
struct WordMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
  bool forward;
  EdgeType edgeType;
  bool big;
  bool skipCurrent;
};
extern const std::vector<WordMotionSpec> WORD_MOTIONS;

// Split by Forward/EdgeType for templated dispatch (EdgeType implicit from vector name)
struct WordMotionSpecNoEdge {
  std::string_view cmd;
  PhysicalKeys keys;
  bool big;
  bool skipCurrent;
};
extern const std::vector<WordMotionSpecNoEdge> FORWARD_NEXTEDGE_MOTIONS;   // w, W
extern const std::vector<WordMotionSpecNoEdge> FORWARD_WORDEDGE_MOTIONS;   // e, E
extern const std::vector<WordMotionSpecNoEdge> BACKWARD_WORDEDGE_MOTIONS;  // b, B
extern const std::vector<WordMotionSpecNoEdge> BACKWARD_NEXTEDGE_MOTIONS;  // ge, gE

// Paragraph motions ({, })
struct ParagraphMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
  bool forward;
};
extern const std::vector<ParagraphMotionSpec> PARAGRAPH_MOTIONS;

// Split by Forward for templated dispatch (EdgeType is always NextEdge for motions)
struct ParagraphMotionSpecNoDir {
  std::string_view cmd;
  PhysicalKeys keys;
};
extern const std::vector<ParagraphMotionSpecNoDir> FORWARD_PARAGRAPH_MOTIONS;   // }
extern const std::vector<ParagraphMotionSpecNoDir> BACKWARD_PARAGRAPH_MOTIONS;  // {

// Sentence motions ((, ))
struct SentenceMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
  bool forward;
};
extern const std::vector<SentenceMotionSpec> SENTENCE_MOTIONS;

// Split by Forward for templated dispatch (EdgeType is always NextEdge for motions)
struct SentenceMotionSpecNoDir {
  std::string_view cmd;
  PhysicalKeys keys;
};
extern const std::vector<SentenceMotionSpecNoDir> FORWARD_SENTENCE_MOTIONS;   // )
extern const std::vector<SentenceMotionSpecNoDir> BACKWARD_SENTENCE_MOTIONS;  // (

// Simple line motions (h, l, 0, ^, $) - same-line only, no boundary checks
struct SimpleLineMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
};
extern const std::vector<SimpleLineMotionSpec> SIMPLE_LINE_MOTIONS;

// Vertical motions (j, k)
struct VerticalMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
  bool isDown;
};
extern const std::vector<VerticalMotionSpec> VERTICAL_MOTIONS;

// Jump motions (gg, G)
struct JumpMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
  bool toStart;  // gg=true, G=false
};
extern const std::vector<JumpMotionSpec> JUMP_MOTIONS;

// Scroll motions (<C-d>, <C-u>)
// NOTE: <C-f> and <C-b> are excluded - see MotionToSpec.cpp for details
struct ScrollMotionSpec {
  std::string_view cmd;
  PhysicalKeys keys;
  int shiftMultiplier;  // +1 for down, -1 for up
  bool isHalf;          // true for C-d/C-u (always true now, kept for future extensibility)
};
extern const std::vector<ScrollMotionSpec> SCROLL_MOTIONS;

// Split by Forward for templated dispatch (direction implicit from vector name)
struct ScrollMotionSpecNoDir {
  std::string_view cmd;
  PhysicalKeys keys;
  bool isHalf;
};
extern const std::vector<ScrollMotionSpecNoDir> FORWARD_SCROLL_MOTIONS;   // <C-d>
extern const std::vector<ScrollMotionSpecNoDir> BACKWARD_SCROLL_MOTIONS;  // <C-u>

} // namespace Motion
