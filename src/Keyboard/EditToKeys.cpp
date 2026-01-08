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

// Word motion edits: operator + motion
const EditToKeys WORD_LEFT         = makeCombinations({{"d", "c"}, {"b"}});
const EditToKeys WORD_RIGHT        = makeCombinations({{"d", "c"}, {"w"}});
const EditToKeys WORD_END_LEFT     = makeCombinations({{"d", "c"}, {"ge"}});
const EditToKeys WORD_END_RIGHT    = makeCombinations({{"d", "c"}, {"e"}});
const EditToKeys BIG_WORD_LEFT     = makeCombinations({{"d", "c"}, {"B"}});
const EditToKeys BIG_WORD_RIGHT    = makeCombinations({{"d", "c"}, {"W"}});
const EditToKeys BIG_WORD_END_LEFT = makeCombinations({{"d", "c"}, {"gE"}});
const EditToKeys BIG_WORD_END_RIGHT= makeCombinations({{"d", "c"}, {"E"}});

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

const EditToKeys LINE_UP = {
    {"O",  {Key::Key_Shift, Key::Key_O}},        // open line above
};

const EditToKeys LINE_DOWN = {
    {"o",  {Key::Key_O}},                        // open line below
};

// --- Navigation motions (for EditOptimizer) ---

const EditToKeys NAV_VERTICAL = {
    {"j",  {Key::Key_J}},                        // move down
    {"k",  {Key::Key_K}},                        // move up
};

const EditToKeys NAV_HORIZONTAL = {
    {"h",  {Key::Key_H}},                        // move left
    {"l",  {Key::Key_L}},                        // move right
};

const EditToKeys NAV_WORD_FWD = {
    {"w",   {Key::Key_W}},                       // next word start
    {"W",   {Key::Key_Shift, Key::Key_W}},       // next WORD start
    {"e",   {Key::Key_E}},                       // word end
    {"E",   {Key::Key_Shift, Key::Key_E}},       // WORD end
};

const EditToKeys NAV_WORD_BWD = {
    {"b",   {Key::Key_B}},                       // prev word start
    {"B",   {Key::Key_Shift, Key::Key_B}},       // prev WORD start
    {"ge",  {Key::Key_G, Key::Key_E}},           // prev word end
    {"gE",  {Key::Key_G, Key::Key_Shift, Key::Key_E}}, // prev WORD end
};

const EditToKeys NAV_LINE_START = {
    {"0",  {Key::Key_0}},                        // line start
    {"^",  {Key::Key_Shift, Key::Key_6}},        // first non-blank
};

const EditToKeys NAV_LINE_END = {
    {"$",  {Key::Key_Shift, Key::Key_4}},        // line end
};

// --- Text object edits ---
// Pattern: operator + inner/around + object
// Using makeCombinations to generate all permutations

const EditToKeys TEXT_OBJ_WORD    = makeCombinations({{"d", "c"}, {"i", "a"}, {"w", "W"}});
const EditToKeys TEXT_OBJ_QUOTE   = makeCombinations({{"d", "c"}, {"i", "a"}, {"\"", "'"}});
const EditToKeys TEXT_OBJ_PAREN   = makeCombinations({{"d", "c"}, {"i", "a"}, {"(", "b"}});   // b = block
const EditToKeys TEXT_OBJ_BRACE   = makeCombinations({{"d", "c"}, {"i", "a"}, {"{", "B"}});   // B = Block
const EditToKeys TEXT_OBJ_BRACKET = makeCombinations({{"d", "c"}, {"i", "a"}, {"["}});
const EditToKeys TEXT_OBJ_ANGLE   = makeCombinations({{"d", "c"}, {"i", "a"}, {"<"}});

} // namespace Normal

// =============================================================================
// Insert Mode Categories
// =============================================================================

namespace Insert {

const EditToKeys CHAR_LEFT = {
    {"<BS>",  {Key::Key_Backspace}},             // backspace
    {"<Esc>", {Key::Key_Esc}},                   // Since escaping shifts the cursor LEFT
    {"<Left>", {Key::Key_Left}},
};

const EditToKeys CHAR_RIGHT = {
    {"<Del>", {Key::Key_Delete}},                // delete at cursor
    {"<Right>", {Key::Key_Right}},
};

const EditToKeys WORD_LEFT = {
    {"<C-w>", {Key::Key_Ctrl, Key::Key_W}},      // delete word before cursor
};

const EditToKeys LINE_LEFT = {
    {"<C-u>", {Key::Key_Ctrl, Key::Key_U}},      // delete to line start
};

const EditToKeys LINE_UP = {
  {"<Up>", {Key::Key_Up }},
};

const EditToKeys LINE_DOWN = {
  {"<Down>", {Key::Key_Down }},
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

const EditToKeys ALL_EDITS_TO_KEYS = combineAll({
    cref(Normal::CHAR_LEFT),
    cref(Normal::CHAR_RIGHT),
    cref(Normal::WORD_LEFT),
    cref(Normal::WORD_RIGHT),
    cref(Normal::WORD_END_LEFT),
    cref(Normal::WORD_END_RIGHT),
    cref(Normal::BIG_WORD_LEFT),
    cref(Normal::BIG_WORD_RIGHT),
    cref(Normal::BIG_WORD_END_LEFT),
    cref(Normal::BIG_WORD_END_RIGHT),
    cref(Normal::LINE_LEFT),
    cref(Normal::LINE_RIGHT),
    cref(Normal::FULL_LINE),
    cref(Normal::LINE_UP),
    cref(Normal::LINE_DOWN),
    cref(Normal::NAV_VERTICAL),
    cref(Normal::NAV_HORIZONTAL),
    cref(Normal::NAV_WORD_FWD),
    cref(Normal::NAV_WORD_BWD),
    cref(Normal::NAV_LINE_START),
    cref(Normal::NAV_LINE_END),
    cref(Normal::TEXT_OBJ_WORD),
    cref(Normal::TEXT_OBJ_QUOTE),
    cref(Normal::TEXT_OBJ_PAREN),
    cref(Normal::TEXT_OBJ_BRACE),
    cref(Normal::TEXT_OBJ_BRACKET),
    cref(Normal::TEXT_OBJ_ANGLE),
    cref(Insert::CHAR_LEFT),
    cref(Insert::CHAR_RIGHT),
    cref(Insert::WORD_LEFT),
    cref(Insert::LINE_LEFT),
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
    if (ALL_EDITS_TO_KEYS.contains(s)) return true;

    // Check if starts with an operator (d, c, y followed by motion/text-object)
    if (s.size() > 1 && OPERATORS.contains(s.substr(0, 1))) return true;

    return false;
}

// =============================================================================
// Pure Deletion Operations (for EditOptimizer deletion model)
// =============================================================================

namespace Deletion {

const EditToKeys CHAR = {
    {"x",  {Key::Key_X}},
    {"X",  {Key::Key_Shift, Key::Key_X}},
};

const EditToKeys WORD = {
    {"dw",  {Key::Key_D, Key::Key_W}},
    {"de",  {Key::Key_D, Key::Key_E}},
    {"db",  {Key::Key_D, Key::Key_B}},
    {"dge", {Key::Key_D, Key::Key_G, Key::Key_E}},
    {"dW",  {Key::Key_D, Key::Key_Shift, Key::Key_W}},
    {"dE",  {Key::Key_D, Key::Key_Shift, Key::Key_E}},
    {"dB",  {Key::Key_D, Key::Key_Shift, Key::Key_B}},
    {"dgE", {Key::Key_D, Key::Key_G, Key::Key_Shift, Key::Key_E}},
};

const EditToKeys LINE = {
    {"dd", {Key::Key_D, Key::Key_D}},
    {"D",  {Key::Key_Shift, Key::Key_D}},
    {"d$", {Key::Key_D, Key::Key_Shift, Key::Key_4}},
    {"d0", {Key::Key_D, Key::Key_0}},
    {"d^", {Key::Key_D, Key::Key_Shift, Key::Key_6}},
};

const EditToKeys TEXT_OBJ = {
    {"diw", {Key::Key_D, Key::Key_I, Key::Key_W}},
    {"daw", {Key::Key_D, Key::Key_A, Key::Key_W}},
    {"diW", {Key::Key_D, Key::Key_I, Key::Key_Shift, Key::Key_W}},
    {"daW", {Key::Key_D, Key::Key_A, Key::Key_Shift, Key::Key_W}},
};

} // namespace Deletion
