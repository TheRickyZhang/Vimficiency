#include "EditToKeys.h"
#include "EditToKeysPrimitives.h"

using namespace std;

namespace EditCategory {

// =============================================================================
// Normal Mode Categories
// =============================================================================

namespace Normal {

const EditToKeys CHAR_LEFT = {
    {"X",  {Key::Key_Shift, Key::Key_X}},       // delete char before cursor
    {"i",  {Key::Key_I}},                        // insert at cursor
};

const EditToKeys CHAR_RIGHT = {
    {"x",  {Key::Key_X}},                        // delete char at cursor
    {"s",  {Key::Key_S}},                        // substitute char
    {"~",  {Key::Key_Shift, Key::Key_Grave}},    // toggle case
    {"a",  {Key::Key_A}},                        // append after cursor
    // Note: r{c} is handled specially in Edit.cpp due to variable second char
};

const EditToKeys WORD_LEFT = {
    {"db", {Key::Key_D, Key::Key_B}},            // delete back to word start
    {"cb", {Key::Key_C, Key::Key_B}},            // change back to word start
};

const EditToKeys WORD_RIGHT = {
    {"dw", {Key::Key_D, Key::Key_W}},            // delete to next word start
    {"cw", {Key::Key_C, Key::Key_W}},            // change to next word start
};

const EditToKeys WORD_END_RIGHT = {
    {"de", {Key::Key_D, Key::Key_E}},            // delete to word end
    {"ce", {Key::Key_C, Key::Key_E}},            // change to word end
};

const EditToKeys BIG_WORD_LEFT = {
    {"dB", {Key::Key_D, Key::Key_Shift, Key::Key_B}},  // delete back to WORD start
    {"cB", {Key::Key_C, Key::Key_Shift, Key::Key_B}},  // change back to WORD start
};

const EditToKeys BIG_WORD_RIGHT = {
    {"dW", {Key::Key_D, Key::Key_Shift, Key::Key_W}},  // delete to next WORD start
    {"cW", {Key::Key_C, Key::Key_Shift, Key::Key_W}},  // change to next WORD start
};

const EditToKeys BIG_WORD_END_RIGHT = {
    {"dE", {Key::Key_D, Key::Key_Shift, Key::Key_E}},  // delete to WORD end
    {"cE", {Key::Key_C, Key::Key_Shift, Key::Key_E}},  // change to WORD end
};

const EditToKeys LINE_LEFT = {
    {"d0", {Key::Key_D, Key::Key_0}},            // delete to line start
    {"d^", {Key::Key_D, Key::Key_Shift, Key::Key_6}},  // delete to first non-blank
    {"c0", {Key::Key_C, Key::Key_0}},            // change to line start
    {"c^", {Key::Key_C, Key::Key_Shift, Key::Key_6}},  // change to first non-blank
    {"I",  {Key::Key_Shift, Key::Key_I}},        // insert at first non-blank
};

const EditToKeys LINE_RIGHT = {
    {"D",  {Key::Key_Shift, Key::Key_D}},        // delete to EOL
    {"C",  {Key::Key_Shift, Key::Key_C}},        // change to EOL
    {"d$", {Key::Key_D, Key::Key_Shift, Key::Key_4}},  // delete to EOL (explicit)
    {"c$", {Key::Key_C, Key::Key_Shift, Key::Key_4}},  // change to EOL (explicit)
    {"A",  {Key::Key_Shift, Key::Key_A}},        // append at EOL
};

const EditToKeys FULL_LINE = {
    {"dd", {Key::Key_D, Key::Key_D}},            // delete entire line
    {"cc", {Key::Key_C, Key::Key_C}},            // change entire line
    {"S",  {Key::Key_Shift, Key::Key_S}},        // substitute line (same as cc)
};

const EditToKeys STRUCTURAL_UP = {
    {"O",  {Key::Key_Shift, Key::Key_O}},        // open line above
};

const EditToKeys STRUCTURAL_DOWN = {
    {"o",  {Key::Key_O}},                        // open line below
};

} // namespace Normal

// =============================================================================
// Insert Mode Categories
// =============================================================================

namespace Insert {

const EditToKeys CHAR_LEFT = {
    {"<BS>",  {Key::Key_Backspace}},             // backspace
};

const EditToKeys CHAR_RIGHT = {
    {"<Del>", {Key::Key_Delete}},                // delete at cursor
};

const EditToKeys WORD_LEFT = {
    {"<C-w>", {Key::Key_Ctrl, Key::Key_W}},      // delete word before cursor
};

const EditToKeys LINE_LEFT = {
    {"<C-u>", {Key::Key_Ctrl, Key::Key_U}},      // delete to line start
};

const EditToKeys NEUTRAL = {
    {"<Esc>", {Key::Key_Esc}},                   // exit insert mode
    {"<CR>",  {Key::Key_Enter}},                 // insert newline
};

} // namespace Insert

// =============================================================================
// Operators
// =============================================================================

const EditToKeys OPERATORS = {
    {"d", {Key::Key_D}},                         // delete operator
    {"c", {Key::Key_C}},                         // change operator
    {"y", {Key::Key_Y}},                         // yank operator
};

} // namespace EditCategory

// =============================================================================
// ALL_EDITS - Union of all edit commands (for parsing/validation)
// =============================================================================

using namespace EditCategory;

const EditToKeys ALL_EDITS = combineAll({
    cref(Normal::CHAR_LEFT),
    cref(Normal::CHAR_RIGHT),
    cref(Normal::WORD_LEFT),
    cref(Normal::WORD_RIGHT),
    cref(Normal::WORD_END_RIGHT),
    cref(Normal::BIG_WORD_LEFT),
    cref(Normal::BIG_WORD_RIGHT),
    cref(Normal::BIG_WORD_END_RIGHT),
    cref(Normal::LINE_LEFT),
    cref(Normal::LINE_RIGHT),
    cref(Normal::FULL_LINE),
    cref(Normal::STRUCTURAL_UP),
    cref(Normal::STRUCTURAL_DOWN),
    cref(Insert::CHAR_LEFT),
    cref(Insert::CHAR_RIGHT),
    cref(Insert::WORD_LEFT),
    cref(Insert::LINE_LEFT),
    cref(Insert::NEUTRAL),
    cref(OPERATORS),
});

// =============================================================================
// Utilities
// =============================================================================

bool isEdit(string_view s) {
    if (s.empty()) return false;

    // Check for r{char} pattern
    if (s.size() == 2 && s[0] == 'r') return true;

    // Check if it's in ALL_EDITS
    if (ALL_EDITS.contains(s)) return true;

    // Check if starts with an operator (d, c, y followed by motion/text-object)
    if (s.size() > 1 && OPERATORS.contains(s.substr(0, 1))) return true;

    return false;
}
