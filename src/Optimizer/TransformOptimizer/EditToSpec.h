#pragma once

#include <vector>

#include "Keyboard/KeyedSequence.h"
#include "VimCore/VimEndpointUtils.h"

// =============================================================================
// Edit Operation Specs - constexpr tables for TransformOptimizer
// =============================================================================
// Structured specs with operation parameters alongside KSId references.
// Separated by direction - isForward is implicit from which vector is used.

namespace Edit {

struct WordEditSpec {
  KSId ksId;
  VimCore::WordOperatorTarget target;
  bool isBig;
};

extern const std::vector<WordEditSpec> FORWARD_WORD_EDITS;
extern const std::vector<WordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS;
extern const std::vector<WordEditSpec> BACKWARD_WORD_EDITS;
extern const std::vector<WordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS;
extern const std::vector<WordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS;

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
