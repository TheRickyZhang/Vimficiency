#include "MotionToSpec.h"
using namespace std;

// =============================================================================
// Motion Operation Specs - tables for MovementOptimizer
// =============================================================================
// Keys from MotionToKeysPrimitives.h, specs from MovementOptimizer.cpp

namespace Motion {

// Word motions: cmd, keys, forward, edgeType, big, skipCurrent
const vector<WordMotionSpec> WORD_MOTIONS = {
    {"w",  {Key::Key_W},                             true,  EdgeType::NextEdge, false, false},
    {"W",  {Key::Key_Shift, Key::Key_W},             true,  EdgeType::NextEdge, true,  false},
    {"b",  {Key::Key_B},                             false, EdgeType::WordEdge, false, true},
    {"B",  {Key::Key_Shift, Key::Key_B},             false, EdgeType::WordEdge, true,  true},
    {"e",  {Key::Key_E},                             true,  EdgeType::WordEdge, false, true},
    {"E",  {Key::Key_Shift, Key::Key_E},             true,  EdgeType::WordEdge, true,  true},
    {"ge", {Key::Key_G, Key::Key_E},                 false, EdgeType::NextEdge, false, false},
    {"gE", {Key::Key_G, Key::Key_Shift, Key::Key_E}, false, EdgeType::NextEdge, true,  false},
};

// Paragraph motions: cmd, keys, forward
const vector<ParagraphMotionSpec> PARAGRAPH_MOTIONS = {
    {"{", {Key::Key_Shift, Key::Key_LBracket}, false},
    {"}", {Key::Key_Shift, Key::Key_RBracket}, true},
};

// Sentence motions: cmd, keys, forward
const vector<SentenceMotionSpec> SENTENCE_MOTIONS = {
    {"(", {Key::Key_Shift, Key::Key_9}, false},
    {")", {Key::Key_Shift, Key::Key_0}, true},
};

// Simple line motions: cmd, keys
const vector<SimpleLineMotionSpec> SIMPLE_LINE_MOTIONS = {
    {"h", {Key::Key_H}},
    {"l", {Key::Key_L}},
    {"0", {Key::Key_0}},
    {"^", {Key::Key_Shift, Key::Key_6}},
    {"$", {Key::Key_Shift, Key::Key_4}},
};

// Vertical motions: cmd, keys, isDown
const vector<VerticalMotionSpec> VERTICAL_MOTIONS = {
    {"j", {Key::Key_J}, true},
    {"k", {Key::Key_K}, false},
};

// Jump motions: cmd, keys, toStart
const vector<JumpMotionSpec> JUMP_MOTIONS = {
    {"gg", {Key::Key_G, Key::Key_G},   true},
    {"G",  {Key::Key_Shift, Key::Key_G}, false},
};

// Scroll motions: cmd, keys, shiftMultiplier, isHalf
const vector<ScrollMotionSpec> SCROLL_MOTIONS = {
    {"<C-d>", {Key::Key_Ctrl, Key::Key_D}, +1, true},   // half-page down
    {"<C-u>", {Key::Key_Ctrl, Key::Key_U}, -1, true},   // half-page up
    {"<C-f>", {Key::Key_Ctrl, Key::Key_F}, +1, false},  // page down
    {"<C-b>", {Key::Key_Ctrl, Key::Key_B}, -1, false},  // page up
};

} // namespace Motion
