#include "EditToSpec.h"
using namespace std;

// =============================================================================
// Edit Operation Specs - constexpr tables for TransformOptimizer
// =============================================================================

namespace Edit {

// Forward word edits: ks{cmd, keys}, edgeType, isBig, skipCurrent
const vector<ForwardWordEditSpec> FORWARD_WORD_EDITS = {
    {KSId::de, EdgeType::WordEdge, false, true},
    {KSId::dw, EdgeType::GapEdge, false, false},
    {KSId::dE, EdgeType::WordEdge, true, true},
    {KSId::dW, EdgeType::GapEdge, true, false},
};
// Subset: de/dE only (dw/dW equivalent to dd on empty lines)
const vector<ForwardWordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS = {
    {KSId::de, EdgeType::WordEdge, false, true},
    {KSId::dE, EdgeType::WordEdge, true, true},
};

// Split by EdgeType for templated dispatch: ks{cmd, keys}, isBig, skipCurrent
const vector<ForwardWordEditSpecNoEdge> FORWARD_WORDEDGE_EDITS = {
    {KSId::de, false, true},
    {KSId::dE, true, true},
};
const vector<ForwardWordEditSpecNoEdge> FORWARD_GAPEDGE_EDITS = {
    {KSId::dw, false, false},
    {KSId::dW, true, false},
};

// Backward word edits: ks{cmd, keys}, edgeType, isBig, skipCurrent, isExclusiveAtCursor
const vector<BackwardWordEditSpec> BACKWARD_WORD_EDITS = {
    {KSId::db, EdgeType::WordEdge, false, true, true},
    {KSId::dge, EdgeType::NextEdge, false, true, false},
    {KSId::dB, EdgeType::WordEdge, true, true, true},
    {KSId::dgE, EdgeType::NextEdge, true, true, false},
};

const vector<BackwardWordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS = {
    {KSId::db, EdgeType::WordEdge, false, true, true},
    {KSId::dB, EdgeType::WordEdge, true, true, true},
};

// Subset: db/dB/dge only (dgE equivalent to dge on empty lines)
const vector<BackwardWordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS = {
    {KSId::db, EdgeType::WordEdge, false, true, true},
    {KSId::dge, EdgeType::NextEdge, false, true, false},
    {KSId::dB, EdgeType::WordEdge, true, true, true},
};

// Split by EdgeType for templated dispatch: ks{cmd, keys}, isBig, skipCurrent, isExclusiveAtCursor
const vector<BackwardWordEditSpecNoEdge> BACKWARD_WORDEDGE_EDITS = {
    {KSId::db, false, true, true},
    {KSId::dB, true, true, true},
};
const vector<BackwardWordEditSpecNoEdge> BACKWARD_NEXTEDGE_EDITS = {
    {KSId::dge, false, true, false},
    {KSId::dgE, true, true, false},
};

const vector<TextObjectEditSpec> TEXT_OBJECT_EDITS = {
    // Small word
    {KSId::diw, true, false},
    {KSId::daw, false, false},
    // Big WORD
    {KSId::diW, true, true},
    {KSId::daW, false, true},
};

// Line motion edits: ks{cmd, keys}, forward
const vector<LineEditSpec> HALF_LINE_EDITS = {
    // Forward (to line end)
    {KSId::D, true},
    // Backward (to line start)
    {KSId::d0, false},
};

// Full line edits: ks{cmd, keys}
const vector<FullLineEditSpec> FULL_LINE_EDITS = {
    {KSId::dd},
};

const vector<FullLineEditSpec> EMPTYLINE_FULL_LINE_EDITS = FULL_LINE_EDITS;

// Paragraph motion edits: ks{cmd, keys}, forward
const vector<ParagraphEditSpec> PARAGRAPH_EDITS = {
    {KSId::dRBrace, true},
    {KSId::dLBrace, false},
};

// Split by Forward for templated dispatch: ks{cmd, keys}
const vector<ParagraphEditSpecNoDir> FORWARD_PARAGRAPH_EDITS = {
    {KSId::dRBrace},
};
const vector<ParagraphEditSpecNoDir> BACKWARD_PARAGRAPH_EDITS = {
    {KSId::dLBrace},
};

// Sentence motion edits: ks{cmd, keys}, forward
const vector<SentenceEditSpec> SENTENCE_EDITS = {
    {KSId::dRParen, true},
    {KSId::dLParen, false},
};

// Split by Forward for templated dispatch: ks{cmd, keys}
const vector<SentenceEditSpecNoDir> FORWARD_SENTENCE_EDITS = {
    {KSId::dRParen},
};
const vector<SentenceEditSpecNoDir> BACKWARD_SENTENCE_EDITS = {
    {KSId::dLParen},
};

} // namespace Edit
