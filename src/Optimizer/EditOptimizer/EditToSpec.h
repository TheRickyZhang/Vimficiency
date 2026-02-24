#pragma once

#include <vector>

#include "Types/EdgeType.h"
#include "Keyboard/KeyedSequence.h"

// =============================================================================
// Edit Operation Specs - constexpr tables for EditOptimizer
// =============================================================================
// Structured specs with operation parameters alongside KSId references.
// Separated by direction - isForward is implicit from which vector is used.

namespace Edit {

// Forward word motion edits (de, dw, dE, dW)
struct ForwardWordEditSpec {
  KSId ksId;
  EdgeType edgeType;
  bool isBig;
  bool skipCurrent;  // de/dE need true; dw/dW need false
};
extern const std::vector<ForwardWordEditSpec> FORWARD_WORD_EDITS;
// Subset: de/dE only (for empty lines where dw/dW equivalent to dd)
extern const std::vector<ForwardWordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS;

// New: Split by EdgeType for templated dispatch (EdgeType implicit from vector name)
struct ForwardWordEditSpecNoEdge {
  KSId ksId;
  bool isBig;
  bool skipCurrent;
};
extern const std::vector<ForwardWordEditSpecNoEdge> FORWARD_WORDEDGE_EDITS;  // de, dE
extern const std::vector<ForwardWordEditSpecNoEdge> FORWARD_GAPEDGE_EDITS;   // dw, dW

// Backward word motion edits (db, dge, dB, dgE)
struct BackwardWordEditSpec {
  KSId ksId;
  EdgeType edgeType;
  bool isBig;
  bool skipCurrent;         // db/dB need true; dge/dgE need false
  bool isExclusiveAtCursor; // db/dB exclude cursor char from deletion
};
extern const std::vector<BackwardWordEditSpec> BACKWARD_WORD_EDITS;
// Subset: db/dB/dge only (dgE equivalent to dge on empty lines)
extern const std::vector<BackwardWordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS;
extern const std::vector<BackwardWordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS;

// New: Split by EdgeType for templated dispatch (EdgeType implicit from vector name)
struct BackwardWordEditSpecNoEdge {
  KSId ksId;
  bool isBig;
  bool skipCurrent;
  bool isExclusiveAtCursor;
};
extern const std::vector<BackwardWordEditSpecNoEdge> BACKWARD_WORDEDGE_EDITS;  // db, dB
extern const std::vector<BackwardWordEditSpecNoEdge> BACKWARD_NEXTEDGE_EDITS;  // dge, dgE

// Text object edits (diw, daw, diW, daW)
struct TextObjectEditSpec {
  KSId ksId;
  bool isInner;
  bool isBig;
};
extern const std::vector<TextObjectEditSpec> TEXT_OBJECT_EDITS;

// Line motion edits (D, d0, d^) - characterwise to line boundary
struct LineEditSpec {
  KSId ksId;
  bool forward;  // true for D/d$, false for d0/d^
};
extern const std::vector<LineEditSpec> HALF_LINE_EDITS;

// Full line edit (dd) - linewise
struct FullLineEditSpec {
  KSId ksId;
};
extern const std::vector<FullLineEditSpec> FULL_LINE_EDITS;
extern const std::vector<FullLineEditSpec> EMPTYLINE_FULL_LINE_EDITS;

// Paragraph motion edits (d}, d{)
struct ParagraphEditSpec {
  KSId ksId;
  bool forward;  // true for d}, false for d{
};
extern const std::vector<ParagraphEditSpec> PARAGRAPH_EDITS;

// Split by Forward for templated dispatch
struct ParagraphEditSpecNoDir {
  KSId ksId;
};
extern const std::vector<ParagraphEditSpecNoDir> FORWARD_PARAGRAPH_EDITS;   // d}
extern const std::vector<ParagraphEditSpecNoDir> BACKWARD_PARAGRAPH_EDITS;  // d{

// Sentence motion edits (d), d()
struct SentenceEditSpec {
  KSId ksId;
  bool forward;  // true for d), false for d(
};
extern const std::vector<SentenceEditSpec> SENTENCE_EDITS;

// Split by Forward for templated dispatch
struct SentenceEditSpecNoDir {
  KSId ksId;
};
extern const std::vector<SentenceEditSpecNoDir> FORWARD_SENTENCE_EDITS;   // d)
extern const std::vector<SentenceEditSpecNoDir> BACKWARD_SENTENCE_EDITS;  // d(

} // namespace Edit
