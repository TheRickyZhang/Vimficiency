#include <vector>

#include "VimCore/EdgeType.h"
#include "Keyboard/KeyboardModel.h"

// =============================================================================
// Edit Operation Specs - constexpr tables for EditOptimizer
// =============================================================================
// Structured specs with operation parameters alongside keys.
// Separated by direction - isForward is implicit from which vector is used.

namespace Edit {

// Forward word motion edits (de, dw, dE, dW)
struct ForwardWordEditSpec {
  const char* cmd;
  PhysicalKeys keys;
  EdgeType edgeType;
  bool isBig;
  bool skipCurrent;  // de/dE need true; dw/dW need false
};
extern const std::vector<ForwardWordEditSpec> FORWARD_WORD_EDITS;
// Subset: de/dE only (for empty lines where dw/dW equivalent to dd)
extern const std::vector<ForwardWordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS;

// Backward word motion edits (db, dge, dB, dgE)
struct BackwardWordEditSpec {
  const char* cmd;
  PhysicalKeys keys;
  EdgeType edgeType;
  bool isBig;
  bool skipCurrent;         // db/dB need true; dge/dgE need false
  bool isExclusiveAtCursor; // db/dB exclude cursor char from deletion
};
extern const std::vector<BackwardWordEditSpec> BACKWARD_WORD_EDITS;
// Subset: db/dB/dge only (dgE equivalent to dge on empty lines)
extern const std::vector<BackwardWordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS;
extern const std::vector<BackwardWordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS;

// Text object edits (diw, daw, diW, daW)
struct TextObjectEditSpec {
  const char* cmd;
  PhysicalKeys keys;
  bool isInner;
  bool isBig;
};
extern const std::vector<TextObjectEditSpec> TEXT_OBJECT_EDITS;

// Line motion edits (D, d0, d^) - characterwise to line boundary
struct LineEditSpec {
  const char* cmd;
  PhysicalKeys keys;
  bool forward;  // true for D/d$, false for d0/d^
};
extern const std::vector<LineEditSpec> HALF_LINE_EDITS;

// Full line edit (dd) - linewise
struct FullLineEditSpec {
  const char* cmd;
  PhysicalKeys keys;
};
extern const std::vector<FullLineEditSpec> FULL_LINE_EDITS;
extern const std::vector<FullLineEditSpec> EMPTYLINE_FULL_LINE_EDITS;

// Paragraph motion edits (d}, d{)
struct ParagraphEditSpec {
  const char* cmd;
  PhysicalKeys keys;
  bool forward;  // true for d}, false for d{
};
extern const std::vector<ParagraphEditSpec> PARAGRAPH_EDITS;

// Sentence motion edits (d), d()
struct SentenceEditSpec {
  const char* cmd;
  PhysicalKeys keys;
  bool forward;  // true for d), false for d(
};
extern const std::vector<SentenceEditSpec> SENTENCE_EDITS;

} // namespace Edit
