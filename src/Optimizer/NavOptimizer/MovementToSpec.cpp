#include "MovementToSpec.h"
using namespace std;

// =============================================================================
// Motion Operation Specs - tables for NavOptimizer
// =============================================================================
// KSId references for bank lookup during A* search

namespace Movement {

// Word motions: ksId, forward, edgeType, big, skipCurrent
const vector<WordMovementSpec> WORD_MOTIONS = {
    {KSId::w,  true,  EdgeType::NextEdge, false, false},
    {KSId::W,  true,  EdgeType::NextEdge, true,  false},
    {KSId::b,  false, EdgeType::WordEdge, false, true},
    {KSId::B,  false, EdgeType::WordEdge, true,  true},
    {KSId::e,  true,  EdgeType::WordEdge, false, true},
    {KSId::E,  true,  EdgeType::WordEdge, true,  true},
    {KSId::ge, false, EdgeType::NextEdge, false, false},
    {KSId::gE, false, EdgeType::NextEdge, true,  false},
};

// Split by Forward/EdgeType for templated dispatch: ksId, big, skipCurrent
const vector<WordMovementSpecNoEdge> FORWARD_NEXTEDGE_MOVEMENTS = {
    {KSId::w, false, false},
    {KSId::W, true,  false},
};
const vector<WordMovementSpecNoEdge> FORWARD_WORDEDGE_MOVEMENTS = {
    {KSId::e, false, true},
    {KSId::E, true,  true},
};
const vector<WordMovementSpecNoEdge> BACKWARD_WORDEDGE_MOVEMENTS = {
    {KSId::b, false, true},
    {KSId::B, true,  true},
};
const vector<WordMovementSpecNoEdge> BACKWARD_NEXTEDGE_MOVEMENTS = {
    {KSId::ge, false, false},
    {KSId::gE, true,  false},
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
