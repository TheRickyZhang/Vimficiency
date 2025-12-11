#include <bits/stdc++.h>
using namespace std;

#include "KeyboardModel.h"

static map<string, vector<Key>> actionToKeys = {
  // letters
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

  // digits
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

  // whitespace
  {" ",  {Key::Key_Space}},
  {"\t", {Key::Key_Tab}},
  {"\n", {Key::Key_Enter}},
  {"\r", {Key::Key_Enter}},

  // number-row punctuation and shifted versions
  {"`", {Key::Key_Grave}},{"~", {Key::Key_Shift, Key::Key_Grave}},
  {"-", {Key::Key_Minus}},{"_", {Key::Key_Shift, Key::Key_Minus}},
  {"=", {Key::Key_Equal}},{"+", {Key::Key_Shift, Key::Key_Equal}},
  {"[", {Key::Key_LBracket}},{"{", {Key::Key_Shift,Key::Key_LBracket}},
  {"]", {Key::Key_RBracket}},{"}", {Key::Key_Shift, Key::Key_RBracket}},
  {"\\",{Key::Key_Backslash}},{"|", {Key::Key_Shift, Key::Key_Backslash}},

  // main-row punctuation and shifted versions
  {";", {Key::Key_Semicolon}},{":", {Key::Key_Shift, Key::Key_Semicolon}},
  {"'", {Key::Key_Apostrophe}},{"\"",{Key::Key_Shift, Key::Key_Apostrophe}},
  {",", {Key::Key_Comma}},{"<", {Key::Key_Shift, Key::Key_Comma}},
  {".", {Key::Key_Period}},{">", {Key::Key_Shift, Key::Key_Period}},
  {"/", {Key::Key_Slash}}, {"?", {Key::Key_Shift, Key::Key_Slash}},

  // shifted digits (!@#$%^&*())
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



static map<string, vector<Key>> motionToKeys = {
  {"h", {Key::Key_H}},
  {"j", {Key::Key_J}},
  {"k", {Key::Key_K}},
  {"l", {Key::Key_L}},

  {"0", {Key::Key_0}},                          // line start
  {"^", {Key::Key_Shift, Key::Key_6}},          // first nonblank
  {"$", {Key::Key_Shift, Key::Key_4}},          // line end

  // ---------------------------------------------------------------------------
  // Word motions (small and big words)
  // ---------------------------------------------------------------------------
  {"w", {Key::Key_W}},
  {"b", {Key::Key_B}},
  {"e", {Key::Key_E}},

  {"W", {Key::Key_Shift, Key::Key_W}},
  {"B", {Key::Key_Shift, Key::Key_B}},
  {"E", {Key::Key_Shift, Key::Key_E}},

  // ---------------------------------------------------------------------------
  // Line / file / screen position motions
  // ---------------------------------------------------------------------------
  {"gg", {Key::Key_G, Key::Key_G}},             // top of file
  {"G",  {Key::Key_Shift, Key::Key_G}},         // goto line / end of file

  {"H",  {Key::Key_Shift, Key::Key_H}},         // top of screen
  {"M",  {Key::Key_Shift, Key::Key_M}},         // middle of screen
  {"L",  {Key::Key_Shift, Key::Key_L}},         // bottom of screen

  // paragraph / sentence-ish motions
  {"{",  {Key::Key_Shift, Key::Key_LBracket}},
  {"}",  {Key::Key_Shift, Key::Key_RBracket}},
  {"(",  {Key::Key_Shift, Key::Key_9}},
  {")",  {Key::Key_Shift, Key::Key_0}},

  // match pairs
  {"%",  {Key::Key_Shift, Key::Key_5}},

  // ---------------------------------------------------------------------------
  // Arrow keys and home/end (for people who use them)
  // ---------------------------------------------------------------------------
  {"<Left>",  {Key::Key_Left}},
  {"<Right>", {Key::Key_Right}},
  {"<Up>",    {Key::Key_Up}},
  {"<Down>",  {Key::Key_Down}},

  {"<Home>",  {Key::Key_Home}},
  {"<End>",   {Key::Key_End}},

  // ---------------------------------------------------------------------------
  // Scrolling (full/half/small)
  // ---------------------------------------------------------------------------
  {"<C-f>", {Key::Key_Ctrl, Key::Key_F}},       // page down
  {"<C-b>", {Key::Key_Ctrl, Key::Key_B}},       // page up
  {"<C-d>", {Key::Key_Ctrl, Key::Key_D}},       // half-page down
  {"<C-u>", {Key::Key_Ctrl, Key::Key_U}},       // half-page up
  {"<C-e>", {Key::Key_Ctrl, Key::Key_E}},       // scroll down one line
  {"<C-y>", {Key::Key_Ctrl, Key::Key_Y}},       // scroll up one line

  // Optional: accept "C-x" notation as synonyms
  {"C-f", {Key::Key_Ctrl, Key::Key_F}},
  {"C-b", {Key::Key_Ctrl, Key::Key_B}},
  {"C-d", {Key::Key_Ctrl, Key::Key_D}},
  {"C-u", {Key::Key_Ctrl, Key::Key_U}},
  {"C-e", {Key::Key_Ctrl, Key::Key_E}},
  {"C-y", {Key::Key_Ctrl, Key::Key_Y}},
};
