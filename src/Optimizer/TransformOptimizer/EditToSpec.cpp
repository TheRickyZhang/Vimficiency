#include "EditToSpec.h"
using namespace std;

// =============================================================================
// Edit Operation Specs - constexpr tables for TransformOptimizer
// =============================================================================

namespace Edit {
using VimCore::WordOperatorTarget;

const vector<WordEditSpec> FORWARD_WORD_EDITS = {
    {KSId::de, WordOperatorTarget::DeleteToWordEnd, false},
    {KSId::dw, WordOperatorTarget::DeleteToNextWord, false},
    {KSId::dE, WordOperatorTarget::DeleteToWordEnd, true},
    {KSId::dW, WordOperatorTarget::DeleteToNextWord, true},
};

const vector<WordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS = {
    {KSId::de, WordOperatorTarget::DeleteToWordEnd, false},
    {KSId::dE, WordOperatorTarget::DeleteToWordEnd, true},
};

const vector<WordEditSpec> BACKWARD_WORD_EDITS = {
    {KSId::db, WordOperatorTarget::DeleteBackToWordBegin, false},
    {KSId::dge, WordOperatorTarget::DeleteBackToWordEnd, false},
    {KSId::dB, WordOperatorTarget::DeleteBackToWordBegin, true},
    {KSId::dgE, WordOperatorTarget::DeleteBackToWordEnd, true},
};

const vector<WordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS = {
    {KSId::db, WordOperatorTarget::DeleteBackToWordBegin, false},
    {KSId::dB, WordOperatorTarget::DeleteBackToWordBegin, true},
};

const vector<WordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS = {
    {KSId::db, WordOperatorTarget::DeleteBackToWordBegin, false},
    {KSId::dge, WordOperatorTarget::DeleteBackToWordEnd, false},
    {KSId::dB, WordOperatorTarget::DeleteBackToWordBegin, true},
};

const vector<TextObjectEditSpec> TEXT_OBJECT_EDITS = {
    // Small word
    {KSId::diw, true, false},
    {KSId::daw, false, false},
    // bigWord
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
