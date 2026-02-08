// SINGLE SOURCE OF TRUTH for static KeyedSequence constants.
// Format: X(Name, "sequence_string", (Key::..., Key::...))
//
// The keys group is parenthesized to handle multi-key combos in the macro.
// Use STRIP_PARENS to expand: (Key::A, Key::B) -> Key::A, Key::B

#define STRIP_PARENS_INNER(...) __VA_ARGS__
#define STRIP_PARENS(x) STRIP_PARENS_INNER x

#define VIMFICIENCY_KEYED_SEQUENCES(X) \
    /* Special keys */ \
    X(BS,     "<BS>",   (Key::Key_Backspace)) \
    X(Del,    "<Del>",  (Key::Key_Delete)) \
    X(Esc,    "<Esc>",  (Key::Key_Esc)) \
    X(CR,     "<CR>",   (Key::Key_Enter)) \
    X(CtrlU,  "<C-u>",  (Key::Key_Ctrl, Key::Key_U)) \
    /* Horizontal motions */ \
    X(h,      "h",      (Key::Key_H)) \
    X(l,      "l",      (Key::Key_L)) \
    X(Zero,   "0",      (Key::Key_0)) \
    X(Caret,  "^",      (Key::Key_Shift, Key::Key_6)) \
    X(Dollar, "$",      (Key::Key_Shift, Key::Key_4)) \
    /* Vertical motions */ \
    X(j,      "j",      (Key::Key_J)) \
    X(k,      "k",      (Key::Key_K)) \
    /* Word motions */ \
    X(w,      "w",      (Key::Key_W)) \
    X(b,      "b",      (Key::Key_B)) \
    X(e,      "e",      (Key::Key_E)) \
    X(ge,     "ge",     (Key::Key_G, Key::Key_E)) \
    X(W,      "W",      (Key::Key_Shift, Key::Key_W)) \
    X(B,      "B",      (Key::Key_Shift, Key::Key_B)) \
    X(E,      "E",      (Key::Key_Shift, Key::Key_E)) \
    X(gE,     "gE",     (Key::Key_G, Key::Key_Shift, Key::Key_E)) \
    /* Jump motions */ \
    X(gg,     "gg",     (Key::Key_G, Key::Key_G)) \
    X(G,      "G",      (Key::Key_Shift, Key::Key_G)) \
    /* Paragraph/Sentence motions */ \
    X(LBrace, "{",      (Key::Key_Shift, Key::Key_LBracket)) \
    X(RBrace, "}",      (Key::Key_Shift, Key::Key_RBracket)) \
    X(LParen, "(",      (Key::Key_Shift, Key::Key_9)) \
    X(RParen, ")",      (Key::Key_Shift, Key::Key_0)) \
    /* Scroll motions */ \
    X(CtrlD,  "<C-d>",  (Key::Key_Ctrl, Key::Key_D))
