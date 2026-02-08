#include "MotionToSpec.h"
using namespace std;

// =============================================================================
// Motion Operation Specs - tables for MotionOptimizer
// =============================================================================
// KSId references for bank lookup during A* search

namespace Motion {

// Word motions: ksId, forward, edgeType, big, skipCurrent
const vector<WordMotionSpec> WORD_MOTIONS = {
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
const vector<WordMotionSpecNoEdge> FORWARD_NEXTEDGE_MOTIONS = {
    {KSId::w, false, false},
    {KSId::W, true,  false},
};
const vector<WordMotionSpecNoEdge> FORWARD_WORDEDGE_MOTIONS = {
    {KSId::e, false, true},
    {KSId::E, true,  true},
};
const vector<WordMotionSpecNoEdge> BACKWARD_WORDEDGE_MOTIONS = {
    {KSId::b, false, true},
    {KSId::B, true,  true},
};
const vector<WordMotionSpecNoEdge> BACKWARD_NEXTEDGE_MOTIONS = {
    {KSId::ge, false, false},
    {KSId::gE, true,  false},
};

// Paragraph motions: ksId, forward
const vector<ParagraphMotionSpec> PARAGRAPH_MOTIONS = {
    {KSId::LBrace, false},
    {KSId::RBrace, true},
};

// Split by Forward for templated dispatch: ksId
const vector<ParagraphMotionSpecNoDir> FORWARD_PARAGRAPH_MOTIONS = {
    {KSId::RBrace},
};
const vector<ParagraphMotionSpecNoDir> BACKWARD_PARAGRAPH_MOTIONS = {
    {KSId::LBrace},
};

// Sentence motions: ksId, forward
const vector<SentenceMotionSpec> SENTENCE_MOTIONS = {
    {KSId::LParen, false},
    {KSId::RParen, true},
};

// Split by Forward for templated dispatch: ksId
const vector<SentenceMotionSpecNoDir> FORWARD_SENTENCE_MOTIONS = {
    {KSId::RParen},
};
const vector<SentenceMotionSpecNoDir> BACKWARD_SENTENCE_MOTIONS = {
    {KSId::LParen},
};

// Simple line motions: ksId
const vector<SimpleLineMotionSpec> SIMPLE_LINE_MOTIONS = {
    {KSId::h},
    {KSId::l},
    {KSId::Zero},
    {KSId::Caret},
    {KSId::Dollar},
};

// Vertical motions: ksId, isDown
const vector<VerticalMotionSpec> VERTICAL_MOTIONS = {
    {KSId::j, true},
    {KSId::k, false},
};

// Jump motions: ksId, toStart
const vector<JumpMotionSpec> JUMP_MOTIONS = {
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
const vector<ScrollMotionSpec> SCROLL_MOTIONS = {
    {KSId::CtrlD, +1, true},   // half-page down
    {KSId::CtrlU, -1, true},   // half-page up
};

// Split by Forward for templated dispatch: ksId, isHalf
const vector<ScrollMotionSpecNoDir> FORWARD_SCROLL_MOTIONS = {
    {KSId::CtrlD, true},
};
const vector<ScrollMotionSpecNoDir> BACKWARD_SCROLL_MOTIONS = {
    {KSId::CtrlU, true},
};

} // namespace Motion
