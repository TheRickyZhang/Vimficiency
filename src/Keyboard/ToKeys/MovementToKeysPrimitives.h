#pragma once

#include "MovementToKeys.h"

// combineAll, combineAllToCharKeySeq, combineAllToList are in CommandToKeys.h

// -------------------- BEGIN Vim Semantic Building Blocks --------------------
const MovementToKeys hjkl = {
  {"h", {Key::Key_H}},
  {"j", {Key::Key_J}},
  {"k", {Key::Key_K}},
  {"l", {Key::Key_L}},
};

const MovementToKeys line_col = {
  {"0", {Key::Key_0}},
  {"^", {Key::Key_Shift, Key::Key_6}},
  {"$", {Key::Key_Shift, Key::Key_4}},
};

const MovementToKeys words = {
  {"w", {Key::Key_W}},
  {"b", {Key::Key_B}},
  {"e", {Key::Key_E}},
  {"ge", {Key::Key_G, Key::Key_E}},

  {"W", {Key::Key_Shift, Key::Key_W}},
  {"B", {Key::Key_Shift, Key::Key_B}},
  {"E", {Key::Key_Shift, Key::Key_E}},
  {"gE", {Key::Key_G, Key::Key_Shift, Key::Key_E}},
};

const MovementToKeys ggG = {
  {"gg", {Key::Key_G, Key::Key_G}},
  {"G",  {Key::Key_Shift, Key::Key_G}},
};

const MovementToKeys brackets = {
  {"{",  {Key::Key_Shift, Key::Key_LBracket}},
  {"}",  {Key::Key_Shift, Key::Key_RBracket}},
  {"(",  {Key::Key_Shift, Key::Key_9}},
  {")",  {Key::Key_Shift, Key::Key_0}},
};
  

const MovementToKeys scrolls = {
  {"<C-f>", {Key::Key_Ctrl, Key::Key_F}},    // page down
  {"<C-b>", {Key::Key_Ctrl, Key::Key_B}},    // page up
  {"<C-d>", {Key::Key_Ctrl, Key::Key_D}},    // half-page down
  {"<C-u>", {Key::Key_Ctrl, Key::Key_U}},    // half-page up
};

// Start edit motions

const MovementToKeys arrows = {
  {"<Up>", {Key::Key_Up}},
  {"<Down>", {Key::Key_Down}},
  {"<Left>", {Key::Key_Left}},
  {"<Right>", {Key::Key_Right}},
};
// -------------------- END Vim Semantic Building Blocks --------------------




// -------------------- BEGIN Categorial Building Blocks --------------------
const MovementToKeys letters = {
  {"a", {Key::Key_A}},{"A", {Key::Key_Shift, Key::Key_A}},
  {"b", {Key::Key_B}},{"B", {Key::Key_Shift, Key::Key_B}},
  {"c", {Key::Key_C}},{"C", {Key::Key_Shift, Key::Key_C}},
  {"d", {Key::Key_D}},{"D", {Key::Key_Shift, Key::Key_D}},
  {"e", {Key::Key_E}},{"E", {Key::Key_Shift, Key::Key_E}},
  {"f", {Key::Key_F}},{"F", {Key::Key_Shift, Key::Key_F}},
  {"g", {Key::Key_G}},{"G", {Key::Key_Shift, Key::Key_G}},
  {"h", {Key::Key_H}},{"H", {Key::Key_Shift, Key::Key_H}},
  {"i", {Key::Key_I}},{"I", {Key::Key_Shift, Key::Key_I}},
  {"j", {Key::Key_J}},{"J", {Key::Key_Shift, Key::Key_J}},
  {"k", {Key::Key_K}},{"K", {Key::Key_Shift, Key::Key_K}},
  {"l", {Key::Key_L}},{"L", {Key::Key_Shift, Key::Key_L}},
  {"m", {Key::Key_M}},{"M", {Key::Key_Shift, Key::Key_M}},
  {"n", {Key::Key_N}},{"N", {Key::Key_Shift, Key::Key_N}},
  {"o", {Key::Key_O}},{"O", {Key::Key_Shift, Key::Key_O}},
  {"p", {Key::Key_P}},{"P", {Key::Key_Shift, Key::Key_P}},
  {"q", {Key::Key_Q}},{"Q", {Key::Key_Shift, Key::Key_Q}},
  {"r", {Key::Key_R}},{"R", {Key::Key_Shift, Key::Key_R}},
  {"s", {Key::Key_S}},{"S", {Key::Key_Shift, Key::Key_S}},
  {"t", {Key::Key_T}},{"T", {Key::Key_Shift, Key::Key_T}},
  {"u", {Key::Key_U}},{"U", {Key::Key_Shift, Key::Key_U}},
  {"v", {Key::Key_V}},{"V", {Key::Key_Shift, Key::Key_V}},
  {"w", {Key::Key_W}},{"W", {Key::Key_Shift, Key::Key_W}},
  {"x", {Key::Key_X}},{"X", {Key::Key_Shift, Key::Key_X}},
  {"y", {Key::Key_Y}},{"Y", {Key::Key_Shift, Key::Key_Y}},
  {"z", {Key::Key_Z}},{"Z", {Key::Key_Shift, Key::Key_Z}},
};

const MovementToKeys digits = {
  {"0", {Key::Key_0}},
  {"1", {Key::Key_1}},
  {"2", {Key::Key_2}},
  {"3", {Key::Key_3}},
  {"4", {Key::Key_4}},
  {"5", {Key::Key_5}},
  {"6", {Key::Key_6}},
  {"7", {Key::Key_7}},
  {"8", {Key::Key_8}},
  {"9", {Key::Key_9}},
};


const MovementToKeys whitespace = {
  {" ",  {Key::Key_Space}},
  {"\t", {Key::Key_Tab}},
  {"\n", {Key::Key_Enter}},
  {"\r", {Key::Key_Enter}},
};


const MovementToKeys topPunctuation = {
  {"`", {Key::Key_Grave}},{"~", {Key::Key_Shift, Key::Key_Grave}},
  {"-", {Key::Key_Minus}},{"_", {Key::Key_Shift, Key::Key_Minus}},
  {"=", {Key::Key_Equal}},{"+", {Key::Key_Shift, Key::Key_Equal}},
  {"[", {Key::Key_LBracket}},{"{", {Key::Key_Shift,Key::Key_LBracket}},
  {"]", {Key::Key_RBracket}},{"}", {Key::Key_Shift, Key::Key_RBracket}},
  {"\\",{Key::Key_Backslash}},{"|", {Key::Key_Shift, Key::Key_Backslash}},
};


const MovementToKeys mainPunctuation = {
  {";", {Key::Key_Semicolon}},{":", {Key::Key_Shift, Key::Key_Semicolon}},
  {"'", {Key::Key_Apostrophe}},{"\"",{Key::Key_Shift, Key::Key_Apostrophe}},
  {",", {Key::Key_Comma}},
  {"<lt>", {Key::Key_Shift, Key::Key_Comma}},
  {"<LT>", {Key::Key_Shift, Key::Key_Comma}},
  {".", {Key::Key_Period}},
  {">", {Key::Key_Shift, Key::Key_Period}},
  {"/", {Key::Key_Slash}}, {"?", {Key::Key_Shift, Key::Key_Slash}},
};

// Same as mainPunctuation but with actual '<' char (for CHAR_TO_KEYS)
const MovementToKeys mainPunctuationSingleChar = {
  {";", {Key::Key_Semicolon}},{":", {Key::Key_Shift, Key::Key_Semicolon}},
  {"'", {Key::Key_Apostrophe}},{"\"",{Key::Key_Shift, Key::Key_Apostrophe}},
  {",", {Key::Key_Comma}},
  {"<", {Key::Key_Shift, Key::Key_Comma}},  // actual '<' character
  {".", {Key::Key_Period}},
  {">", {Key::Key_Shift, Key::Key_Period}},
  {"/", {Key::Key_Slash}}, {"?", {Key::Key_Shift, Key::Key_Slash}},
};


const MovementToKeys digitSymbols = {
  {"!", {Key::Key_Shift, Key::Key_1}},
  {"@", {Key::Key_Shift, Key::Key_2}},
  {"#", {Key::Key_Shift, Key::Key_3}},
  {"$", {Key::Key_Shift, Key::Key_4}},
  {"%", {Key::Key_Shift, Key::Key_5}},
  {"^", {Key::Key_Shift, Key::Key_6}},
  {"&", {Key::Key_Shift, Key::Key_7}},
  {"*", {Key::Key_Shift, Key::Key_8}},
  {"(", {Key::Key_Shift, Key::Key_9}},
  {")", {Key::Key_Shift, Key::Key_0}},
};

const MovementToKeys allSymbolsAndPunctuation = combineAll({
  topPunctuation,
  mainPunctuation,
  digitSymbols,
});

// Single-char version for CHAR_TO_KEYS (uses '<' instead of key notation)
const MovementToKeys allSingleCharPunctuationAndSymbols = combineAll({
  topPunctuation,
  mainPunctuationSingleChar,
  digitSymbols,
});

const MovementToKeys specialWithBracket = {
    {"<Space>", {Key::Key_Space}},
  {"<Tab>", {Key::Key_Tab}},
  {"<CR>", {Key::Key_Enter}},
  {"<Enter>", {Key::Key_Enter}},
  {"<Return>", {Key::Key_Enter}},
  {"<Esc>", {Key::Key_Esc}},
  {"<BS>", {Key::Key_Backspace}},
  {"<Del>", {Key::Key_Delete}},

  {"<Up>", {Key::Key_Up}},
  {"<Down>", {Key::Key_Down}},
  {"<Left>", {Key::Key_Left}},
  {"<Right>", {Key::Key_Right}},

  {"<Home>", {Key::Key_Home}},
  {"<End>", {Key::Key_End}},
};

const MovementToKeys ctrlCombinations = {
  {"<C-a>", {Key::Key_Ctrl, Key::Key_A}},
  {"<C-b>", {Key::Key_Ctrl, Key::Key_B}},
  {"<C-c>", {Key::Key_Ctrl, Key::Key_C}},
  {"<C-d>", {Key::Key_Ctrl, Key::Key_D}},
  {"<C-e>", {Key::Key_Ctrl, Key::Key_E}},
  {"<C-f>", {Key::Key_Ctrl, Key::Key_F}},
  {"<C-g>", {Key::Key_Ctrl, Key::Key_G}},
  {"<C-h>", {Key::Key_Ctrl, Key::Key_H}},
  {"<C-i>", {Key::Key_Ctrl, Key::Key_I}},
  {"<C-j>", {Key::Key_Ctrl, Key::Key_J}},
  {"<C-k>", {Key::Key_Ctrl, Key::Key_K}},
  {"<C-l>", {Key::Key_Ctrl, Key::Key_L}},
  {"<C-m>", {Key::Key_Ctrl, Key::Key_M}},
  {"<C-n>", {Key::Key_Ctrl, Key::Key_N}},
  {"<C-o>", {Key::Key_Ctrl, Key::Key_O}},
  {"<C-p>", {Key::Key_Ctrl, Key::Key_P}},
  {"<C-q>", {Key::Key_Ctrl, Key::Key_Q}},
  {"<C-r>", {Key::Key_Ctrl, Key::Key_R}},
  {"<C-s>", {Key::Key_Ctrl, Key::Key_S}},
  {"<C-t>", {Key::Key_Ctrl, Key::Key_T}},
  {"<C-u>", {Key::Key_Ctrl, Key::Key_U}},
  {"<C-v>", {Key::Key_Ctrl, Key::Key_V}},
  {"<C-w>", {Key::Key_Ctrl, Key::Key_W}},
  {"<C-x>", {Key::Key_Ctrl, Key::Key_X}},
  {"<C-y>", {Key::Key_Ctrl, Key::Key_Y}},
  {"<C-z>", {Key::Key_Ctrl, Key::Key_Z}},

  {"<C-Space>", {Key::Key_Ctrl, Key::Key_Space}},
  {"<C-BS>", {Key::Key_Ctrl, Key::Key_Backspace}},
  {"<C-CR>", {Key::Key_Ctrl, Key::Key_Enter}},
  {"<C-Tab>", {Key::Key_Ctrl, Key::Key_Tab}},
};
// -------------------- END Categorial Building Blocks --------------------
