// SINGLE SOURCE OF TRUTH for static KeyedSequence constants.
// Format: _KS(Name, "sequence_string", (Key::..., Key::...))
//
// The keys group is parenthesized to handle multi-key combos in the macro.
// Use STRIP_PARENS to expand: (Key::A, Key::B) -> Key::A, Key::B
//
// NOTE: The callback parameter is named _KS (not X) to avoid collision with
// entry names that match single letters (e.g., the X command).

#define STRIP_PARENS_INNER(...) __VA_ARGS__
#define STRIP_PARENS(x) STRIP_PARENS_INNER x

#define VF_KEYED_SEQUENCES(_KS) \
    /* Special keys */ \
    _KS(BS,     "<BS>",   (Key::Key_Backspace)) \
    _KS(Del,    "<Del>",  (Key::Key_Delete)) \
    _KS(Esc,    "<Esc>",  (Key::Key_Esc)) \
    _KS(CR,     "<CR>",   (Key::Key_Enter)) \
    _KS(CtrlU,  "<C-u>",  (Key::Key_Ctrl, Key::Key_U)) \
    /* Horizontal motions */ \
    _KS(h,      "h",      (Key::Key_H)) \
    _KS(l,      "l",      (Key::Key_L)) \
    _KS(Zero,   "0",      (Key::Key_0)) \
    _KS(Caret,  "^",      (Key::Key_Shift, Key::Key_6)) \
    _KS(Dollar, "$",      (Key::Key_Shift, Key::Key_4)) \
    /* Vertical motions */ \
    _KS(j,      "j",      (Key::Key_J)) \
    _KS(k,      "k",      (Key::Key_K)) \
    /* Word motions */ \
    _KS(w,      "w",      (Key::Key_W)) \
    _KS(b,      "b",      (Key::Key_B)) \
    _KS(e,      "e",      (Key::Key_E)) \
    _KS(ge,     "ge",     (Key::Key_G, Key::Key_E)) \
    _KS(W,      "W",      (Key::Key_Shift, Key::Key_W)) \
    _KS(B,      "B",      (Key::Key_Shift, Key::Key_B)) \
    _KS(E,      "E",      (Key::Key_Shift, Key::Key_E)) \
    _KS(gE,     "gE",     (Key::Key_G, Key::Key_Shift, Key::Key_E)) \
    /* Jump motions */ \
    _KS(gg,     "gg",     (Key::Key_G, Key::Key_G)) \
    _KS(G,      "G",      (Key::Key_Shift, Key::Key_G)) \
    /* Paragraph/Sentence motions */ \
    _KS(LBrace, "{",      (Key::Key_Shift, Key::Key_LBracket)) \
    _KS(RBrace, "}",      (Key::Key_Shift, Key::Key_RBracket)) \
    _KS(LParen, "(",      (Key::Key_Shift, Key::Key_9)) \
    _KS(RParen, ")",      (Key::Key_Shift, Key::Key_0)) \
    /* Scroll motions */ \
    _KS(CtrlD,  "<C-d>",  (Key::Key_Ctrl, Key::Key_D)) \
    /* Dot repeat */ \
    _KS(Period, ".",       (Key::Key_Period)) \
    /* Normal-mode commands */ \
    _KS(f,      "f",      (Key::Key_F)) \
    _KS(r,      "r",      (Key::Key_R)) \
    _KS(i,      "i",      (Key::Key_I)) \
    _KS(A,      "A",      (Key::Key_Shift, Key::Key_A)) \
    _KS(s,      "s",      (Key::Key_S)) \
    _KS(C,      "C",      (Key::Key_Shift, Key::Key_C)) \
    _KS(cc,     "cc",     (Key::Key_C, Key::Key_C)) \
    _KS(hs,     "hs",     (Key::Key_H, Key::Key_S)) \
    _KS(ZeroC,  "0C",     (Key::Key_0, Key::Key_Shift, Key::Key_C)) \
    /* Edit commands */ \
    _KS(x,      "x",      (Key::Key_X)) \
    _KS(X,      "X",      (Key::Key_Shift, Key::Key_X)) \
    _KS(J,      "J",      (Key::Key_Shift, Key::Key_J)) \
    _KS(gJ,     "gJ",     (Key::Key_G, Key::Key_Shift, Key::Key_J)) \
    _KS(D,      "D",      (Key::Key_Shift, Key::Key_D)) \
    _KS(d0,     "d0",     (Key::Key_D, Key::Key_0)) \
    _KS(dd,     "dd",     (Key::Key_D, Key::Key_D)) \
    _KS(dj,     "dj",     (Key::Key_D, Key::Key_J)) \
    _KS(dk,     "dk",     (Key::Key_D, Key::Key_K)) \
    _KS(de,     "de",     (Key::Key_D, Key::Key_E)) \
    _KS(dw,     "dw",     (Key::Key_D, Key::Key_W)) \
    _KS(dE,     "dE",     (Key::Key_D, Key::Key_Shift, Key::Key_E)) \
    _KS(dW,     "dW",     (Key::Key_D, Key::Key_Shift, Key::Key_W)) \
    _KS(db,     "db",     (Key::Key_D, Key::Key_B)) \
    _KS(dge,    "dge",    (Key::Key_D, Key::Key_G, Key::Key_E)) \
    _KS(dB,     "dB",     (Key::Key_D, Key::Key_Shift, Key::Key_B)) \
    _KS(dgE,    "dgE",    (Key::Key_D, Key::Key_G, Key::Key_Shift, Key::Key_E)) \
    _KS(diw,    "diw",    (Key::Key_D, Key::Key_I, Key::Key_W)) \
    _KS(daw,    "daw",    (Key::Key_D, Key::Key_A, Key::Key_W)) \
    _KS(diW,    "diW",    (Key::Key_D, Key::Key_I, Key::Key_Shift, Key::Key_W)) \
    _KS(daW,    "daW",    (Key::Key_D, Key::Key_A, Key::Key_Shift, Key::Key_W)) \
    _KS(dRBrace,"d}",     (Key::Key_D, Key::Key_Shift, Key::Key_RBracket)) \
    _KS(dLBrace,"d{",     (Key::Key_D, Key::Key_Shift, Key::Key_LBracket)) \
    _KS(dRParen,"d)",     (Key::Key_D, Key::Key_Shift, Key::Key_0)) \
    _KS(dLParen,"d(",     (Key::Key_D, Key::Key_Shift, Key::Key_9))
