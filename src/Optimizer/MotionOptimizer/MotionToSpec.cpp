#include "MotionToSpec.h"
using namespace std;
using KS = KeyedSequence;

// =============================================================================
// Motion Operation Specs - tables for MotionOptimizer
// =============================================================================
// Keys from KeyedSequence motion constants

namespace Motion {

// Word motions: ks, forward, edgeType, big, skipCurrent
const vector<WordMotionSpec> WORD_MOTIONS = {
    {KS::w,  true,  EdgeType::NextEdge, false, false},
    {KS::W,  true,  EdgeType::NextEdge, true,  false},
    {KS::b,  false, EdgeType::WordEdge, false, true},
    {KS::B,  false, EdgeType::WordEdge, true,  true},
    {KS::e,  true,  EdgeType::WordEdge, false, true},
    {KS::E,  true,  EdgeType::WordEdge, true,  true},
    {KS::ge, false, EdgeType::NextEdge, false, false},
    {KS::gE, false, EdgeType::NextEdge, true,  false},
};

// Split by Forward/EdgeType for templated dispatch: ks, big, skipCurrent
const vector<WordMotionSpecNoEdge> FORWARD_NEXTEDGE_MOTIONS = {
    {KS::w, false, false},
    {KS::W, true,  false},
};
const vector<WordMotionSpecNoEdge> FORWARD_WORDEDGE_MOTIONS = {
    {KS::e, false, true},
    {KS::E, true,  true},
};
const vector<WordMotionSpecNoEdge> BACKWARD_WORDEDGE_MOTIONS = {
    {KS::b, false, true},
    {KS::B, true,  true},
};
const vector<WordMotionSpecNoEdge> BACKWARD_NEXTEDGE_MOTIONS = {
    {KS::ge, false, false},
    {KS::gE, true,  false},
};

// Paragraph motions: ks, forward
const vector<ParagraphMotionSpec> PARAGRAPH_MOTIONS = {
    {KS::LBrace, false},
    {KS::RBrace, true},
};

// Split by Forward for templated dispatch: ks
const vector<ParagraphMotionSpecNoDir> FORWARD_PARAGRAPH_MOTIONS = {
    {KS::RBrace},
};
const vector<ParagraphMotionSpecNoDir> BACKWARD_PARAGRAPH_MOTIONS = {
    {KS::LBrace},
};

// Sentence motions: ks, forward
const vector<SentenceMotionSpec> SENTENCE_MOTIONS = {
    {KS::LParen, false},
    {KS::RParen, true},
};

// Split by Forward for templated dispatch: ks
const vector<SentenceMotionSpecNoDir> FORWARD_SENTENCE_MOTIONS = {
    {KS::RParen},
};
const vector<SentenceMotionSpecNoDir> BACKWARD_SENTENCE_MOTIONS = {
    {KS::LParen},
};

// Simple line motions: ks
const vector<SimpleLineMotionSpec> SIMPLE_LINE_MOTIONS = {
    {KS::h},
    {KS::l},
    {KS::Zero},
    {KS::Caret},
    {KS::Dollar},
};

// Vertical motions: ks, isDown
const vector<VerticalMotionSpec> VERTICAL_MOTIONS = {
    {KS::j, true},
    {KS::k, false},
};

// Jump motions: ks, toStart
const vector<JumpMotionSpec> JUMP_MOTIONS = {
    {KS::gg, true},
    {KS::G,  false},
};

// Scroll motions: ks, shiftMultiplier, isHalf
// NOTE: <C-f> and <C-b> (full-page scroll) are NOT included because they depend on
// viewport state that we don't track. These commands scroll the window, and when the
// buffer is smaller than the window or cursor is near buffer edges, they may not move
// the cursor at all (nothing to scroll). To support them properly, we would need to:
// 1. Track viewport state (top visible line, window height)
// 2. Simulate actual scroll behavior (viewport movement + cursor adjustment)
// 3. Handle edge cases where scrolling is not possible
// <C-d> and <C-u> are more predictable as they move by a fixed scroll amount.
const vector<ScrollMotionSpec> SCROLL_MOTIONS = {
    {KS::CtrlD, +1, true},   // half-page down
    {KS::CtrlU, -1, true},   // half-page up
};

// Split by Forward for templated dispatch: ks, isHalf
const vector<ScrollMotionSpecNoDir> FORWARD_SCROLL_MOTIONS = {
    {KS::CtrlD, true},
};
const vector<ScrollMotionSpecNoDir> BACKWARD_SCROLL_MOTIONS = {
    {KS::CtrlU, true},
};

} // namespace Motion
