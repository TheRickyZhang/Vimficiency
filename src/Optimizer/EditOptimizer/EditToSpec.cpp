#include "EditToSpec.h"
using namespace std;

// =============================================================================
// Edit Operation Specs - constexpr tables for EditOptimizer
// =============================================================================

namespace Edit {

// Forward word edits: ks{cmd, keys}, edgeType, isBig, skipCurrent
const vector<ForwardWordEditSpec> FORWARD_WORD_EDITS = {
    {{"de", {Key::Key_D, Key::Key_E}}, EdgeType::WordEdge, false, true},
    {{"dw", {Key::Key_D, Key::Key_W}}, EdgeType::GapEdge, false, false},
    {{"dE", {Key::Key_D, Key::Key_Shift, Key::Key_E}}, EdgeType::WordEdge, true, true},
    {{"dW", {Key::Key_D, Key::Key_Shift, Key::Key_W}}, EdgeType::GapEdge, true, false},
};
// Subset: de/dE only (dw/dW equivalent to dd on empty lines)
const vector<ForwardWordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS = {
    {{"de", {Key::Key_D, Key::Key_E}}, EdgeType::WordEdge, false, true},
    {{"dE", {Key::Key_D, Key::Key_Shift, Key::Key_E}}, EdgeType::WordEdge, true, true},
};

// Split by EdgeType for templated dispatch: ks{cmd, keys}, isBig, skipCurrent
const vector<ForwardWordEditSpecNoEdge> FORWARD_WORDEDGE_EDITS = {
    {{"de", {Key::Key_D, Key::Key_E}}, false, true},
    {{"dE", {Key::Key_D, Key::Key_Shift, Key::Key_E}}, true, true},
};
const vector<ForwardWordEditSpecNoEdge> FORWARD_GAPEDGE_EDITS = {
    {{"dw", {Key::Key_D, Key::Key_W}}, false, false},
    {{"dW", {Key::Key_D, Key::Key_Shift, Key::Key_W}}, true, false},
};

// Backward word edits: ks{cmd, keys}, edgeType, isBig, skipCurrent, isExclusiveAtCursor
const vector<BackwardWordEditSpec> BACKWARD_WORD_EDITS = {
    {{"db", {Key::Key_D, Key::Key_B}}, EdgeType::WordEdge, false, true, true},
    {{"dge", {Key::Key_D, Key::Key_G, Key::Key_E}}, EdgeType::NextEdge, false, true, false},
    {{"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}}, EdgeType::WordEdge, true, true, true},
    {{"dgE", {Key::Key_D, Key::Key_G, Key::Key_Shift, Key::Key_E}}, EdgeType::NextEdge, true, true, false},
};

const vector<BackwardWordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS = {
    {{"db", {Key::Key_D, Key::Key_B}}, EdgeType::WordEdge, false, true, true},
    {{"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}}, EdgeType::WordEdge, true, true, true},
};

// Subset: db/dB/dge only (dgE equivalent to dge on empty lines)
const vector<BackwardWordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS = {
    {{"db", {Key::Key_D, Key::Key_B}}, EdgeType::WordEdge, false, true, true},
    {{"dge", {Key::Key_D, Key::Key_G, Key::Key_E}}, EdgeType::NextEdge, false, true, false},
    {{"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}}, EdgeType::WordEdge, true, true, true},
};

// Split by EdgeType for templated dispatch: ks{cmd, keys}, isBig, skipCurrent, isExclusiveAtCursor
const vector<BackwardWordEditSpecNoEdge> BACKWARD_WORDEDGE_EDITS = {
    {{"db", {Key::Key_D, Key::Key_B}}, false, true, true},
    {{"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}}, true, true, true},
};
const vector<BackwardWordEditSpecNoEdge> BACKWARD_NEXTEDGE_EDITS = {
    {{"dge", {Key::Key_D, Key::Key_G, Key::Key_E}}, false, true, false},
    {{"dgE", {Key::Key_D, Key::Key_G, Key::Key_Shift, Key::Key_E}}, true, true, false},
};

const vector<TextObjectEditSpec> TEXT_OBJECT_EDITS = {
    // Small word
    {{"diw", {Key::Key_D, Key::Key_I, Key::Key_W}}, true, false},
    {{"daw", {Key::Key_D, Key::Key_A, Key::Key_W}}, false, false},
    // Big WORD
    {{"diW", {Key::Key_D, Key::Key_I, Key::Key_Shift, Key::Key_W}}, true, true},
    {{"daW", {Key::Key_D, Key::Key_A, Key::Key_Shift, Key::Key_W}}, false, true},
};

// Line motion edits: ks{cmd, keys}, forward
const vector<LineEditSpec> HALF_LINE_EDITS = {
    // Forward (to line end)
    {{"D", {Key::Key_Shift, Key::Key_D}}, true},
    // Backward (to line start)
    {{"d0", {Key::Key_D, Key::Key_0}}, false},
};

// Full line edits: ks{cmd, keys}
const vector<FullLineEditSpec> FULL_LINE_EDITS = {
    {{"dd", {Key::Key_D, Key::Key_D}}},
};

const vector<FullLineEditSpec> EMPTYLINE_FULL_LINE_EDITS = FULL_LINE_EDITS;

// Paragraph motion edits: ks{cmd, keys}, forward
const vector<ParagraphEditSpec> PARAGRAPH_EDITS = {
    {{"d}", {Key::Key_D, Key::Key_Shift, Key::Key_RBracket}}, true},
    {{"d{", {Key::Key_D, Key::Key_Shift, Key::Key_LBracket}}, false},
};

// Split by Forward for templated dispatch: ks{cmd, keys}
const vector<ParagraphEditSpecNoDir> FORWARD_PARAGRAPH_EDITS = {
    {{"d}", {Key::Key_D, Key::Key_Shift, Key::Key_RBracket}}},
};
const vector<ParagraphEditSpecNoDir> BACKWARD_PARAGRAPH_EDITS = {
    {{"d{", {Key::Key_D, Key::Key_Shift, Key::Key_LBracket}}},
};

// Sentence motion edits: ks{cmd, keys}, forward
const vector<SentenceEditSpec> SENTENCE_EDITS = {
    {{"d)", {Key::Key_D, Key::Key_Shift, Key::Key_0}}, true},
    {{"d(", {Key::Key_D, Key::Key_Shift, Key::Key_9}}, false},
};

// Split by Forward for templated dispatch: ks{cmd, keys}
const vector<SentenceEditSpecNoDir> FORWARD_SENTENCE_EDITS = {
    {{"d)", {Key::Key_D, Key::Key_Shift, Key::Key_0}}},
};
const vector<SentenceEditSpecNoDir> BACKWARD_SENTENCE_EDITS = {
    {{"d(", {Key::Key_D, Key::Key_Shift, Key::Key_9}}},
};

} // namespace Edit
