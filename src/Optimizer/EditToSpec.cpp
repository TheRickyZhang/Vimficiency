#include "EditToSpec.h"
using namespace std;

// =============================================================================
// Edit Operation Specs - constexpr tables for EditOptimizer
// =============================================================================

namespace Edit {

// Forward word edits: cmd, keys, edgeType, isBig, skipCurrent
const vector<ForwardWordEditSpec> FORWARD_WORD_EDITS = {
    {"de", {Key::Key_D, Key::Key_E}, EdgeType::WordEdge, false, true},
    {"dw", {Key::Key_D, Key::Key_W}, EdgeType::GapEdge, false, false},
    {"dE", {Key::Key_D, Key::Key_Shift, Key::Key_E}, EdgeType::WordEdge, true, true},
    {"dW", {Key::Key_D, Key::Key_Shift, Key::Key_W}, EdgeType::GapEdge, true, false},
};
// Subset: de/dE only (dw/dW equivalent to dd on empty lines)
const vector<ForwardWordEditSpec> EMPTYLINE_FORWARD_WORD_EDITS = {
    {"de", {Key::Key_D, Key::Key_E}, EdgeType::WordEdge, false, true},
    {"dE", {Key::Key_D, Key::Key_Shift, Key::Key_E}, EdgeType::WordEdge, true, true},
};

// Backward word edits: cmd, keys, edgeType, isBig, skipCurrent, isExclusiveAtCursor
const vector<BackwardWordEditSpec> BACKWARD_WORD_EDITS = {
    {"db", {Key::Key_D, Key::Key_B}, EdgeType::WordEdge, false, true, true},
    {"dge", {Key::Key_D, Key::Key_G, Key::Key_E}, EdgeType::NextEdge, false, true, false},
    {"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}, EdgeType::WordEdge, true, true, true},
    {"dgE", {Key::Key_D, Key::Key_G, Key::Key_Shift, Key::Key_E}, EdgeType::NextEdge, true, true, false},
};


const vector<BackwardWordEditSpec> EXCLUSIVE_BACKWARD_WORD_EDITS = {
    {"db", {Key::Key_D, Key::Key_B}, EdgeType::WordEdge, false, true, true},
    {"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}, EdgeType::WordEdge, true, true, true},
};

// Subset: db/dB/dge only (dgE equivalent to dge on empty lines)
const vector<BackwardWordEditSpec> EMPTYLINE_BACKWARD_WORD_EDITS = {
    {"db", {Key::Key_D, Key::Key_B}, EdgeType::WordEdge, false, true, true},
    {"dge", {Key::Key_D, Key::Key_G, Key::Key_E}, EdgeType::NextEdge, false, true, false},
    {"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}, EdgeType::WordEdge, true, true, true},
};

const vector<TextObjectEditSpec> TEXT_OBJECT_EDITS = {
    // Small word
    {"diw", {Key::Key_D, Key::Key_I, Key::Key_W}, true, false},
    {"daw", {Key::Key_D, Key::Key_A, Key::Key_W}, false, false},
    // Big WORD
    {"diW", {Key::Key_D, Key::Key_I, Key::Key_Shift, Key::Key_W}, true, true},
    {"daW", {Key::Key_D, Key::Key_A, Key::Key_Shift, Key::Key_W}, false, true},
};

// Line motion edits: cmd, keys, forward
const vector<LineEditSpec> HALF_LINE_EDITS = {
    // Forward (to line end)
    {"D", {Key::Key_Shift, Key::Key_D}, true},
    // Backward (to line start)
    {"d0", {Key::Key_D, Key::Key_0}, false},
};

// Full line edits: cmd, keys
const vector<FullLineEditSpec> FULL_LINE_EDITS = {
    {"dd", {Key::Key_D, Key::Key_D}},
};

const vector<FullLineEditSpec> EMPTYLINE_FULL_LINE_EDITS = FULL_LINE_EDITS;

} // namespace Edit
