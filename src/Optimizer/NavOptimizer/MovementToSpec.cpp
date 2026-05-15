#include "MovementToSpec.h"
using namespace std;

// =============================================================================
// Motion Operation Specs - tables for NavOptimizer
// =============================================================================
// KSId references for bank lookup during A* search

namespace Movement {
using VimCore::WordMotionTarget;

const vector<WordMovementSpec> WORD_MOTIONS = {
    {KSId::w,  WordMotionTarget::NextBegin, false},
    {KSId::W,  WordMotionTarget::NextBegin, true},
    {KSId::b,  WordMotionTarget::PreviousBegin, false},
    {KSId::B,  WordMotionTarget::PreviousBegin, true},
    {KSId::e,  WordMotionTarget::NextEnd, false},
    {KSId::E,  WordMotionTarget::NextEnd, true},
    {KSId::ge, WordMotionTarget::PreviousEnd, false},
    {KSId::gE, WordMotionTarget::PreviousEnd, true},
};

const vector<WordMovementSpec> FORWARD_WORD_MOVEMENTS = {
    {KSId::w, WordMotionTarget::NextBegin, false},
    {KSId::W, WordMotionTarget::NextBegin, true},
    {KSId::e, WordMotionTarget::NextEnd, false},
    {KSId::E, WordMotionTarget::NextEnd, true},
};

const vector<WordMovementSpec> BACKWARD_WORD_MOVEMENTS = {
    {KSId::b, WordMotionTarget::PreviousBegin, false},
    {KSId::B, WordMotionTarget::PreviousBegin, true},
    {KSId::ge, WordMotionTarget::PreviousEnd, false},
    {KSId::gE, WordMotionTarget::PreviousEnd, true},
};

// Paragraph motions: ksId, forward
const vector<ParagraphMovementSpec> PARAGRAPH_MOTIONS = {
    {KSId::LBrace, false},
    {KSId::RBrace, true},
};

// Split by Forward for templated dispatch: ksId
const vector<ParagraphMovementSpecNoDir> FORWARD_PARAGRAPH_MOVEMENTS = {
    {KSId::RBrace},
};
const vector<ParagraphMovementSpecNoDir> BACKWARD_PARAGRAPH_MOVEMENTS = {
    {KSId::LBrace},
};

// Sentence motions: ksId, forward
const vector<SentenceMovementSpec> SENTENCE_MOTIONS = {
    {KSId::LParen, false},
    {KSId::RParen, true},
};

// Split by Forward for templated dispatch: ksId
const vector<SentenceMovementSpecNoDir> FORWARD_SENTENCE_MOVEMENTS = {
    {KSId::RParen},
};
const vector<SentenceMovementSpecNoDir> BACKWARD_SENTENCE_MOVEMENTS = {
    {KSId::LParen},
};

// Simple line motions: ksId
const vector<SimpleLineMovementSpec> SIMPLE_LINE_MOTIONS = {
    {KSId::h},
    {KSId::l},
    {KSId::Zero},
    {KSId::Caret},
    {KSId::Dollar},
};

// Vertical motions: ksId, isDown
const vector<VerticalMovementSpec> VERTICAL_MOTIONS = {
    {KSId::j, true},
    {KSId::k, false},
};

// Jump motions: ksId, toStart
const vector<JumpMovementSpec> JUMP_MOTIONS = {
    {KSId::gg, true},
    {KSId::G,  false},
};

// Scroll motions: ksId, shiftMultiplier, isHalf
// NOTE: <C-f> and <C-b> (full-page scroll) are NOT included because they depend on
// viewport state that we don't track. These commands scroll the window, and when the
// buffer is smaller than the window or cursor is near buffer edges, they may not move
// the cursor at all (nothing to scroll). To support them properly, we would need to:
// 1. Track viewport state (top visible line, window height)
// 2. Simulate actual scroll behavior (viewport movement + cursor adjustment)
// 3. Handle edge cases where scrolling is not possible
// <C-d> and <C-u> are more predictable as they move by a fixed scroll amount.
const vector<ScrollMovementSpec> SCROLL_MOTIONS = {
    {KSId::CtrlD, +1, true},   // half-page down
    {KSId::CtrlU, -1, true},   // half-page up
};

// Split by Forward for templated dispatch: ksId, isHalf
const vector<ScrollMovementSpecNoDir> FORWARD_SCROLL_MOVEMENTS = {
    {KSId::CtrlD, true},
};
const vector<ScrollMovementSpecNoDir> BACKWARD_SCROLL_MOVEMENTS = {
    {KSId::CtrlU, true},
};

} // namespace Movement
