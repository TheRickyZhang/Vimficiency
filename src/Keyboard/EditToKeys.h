#pragma once

#include "Keyboard/StringToKeys.h"

// Edit string to PhysicalKeys mapping (parallel to MotionToKeys)
using EditToKeys = StringToKeys;

// =============================================================================
// Edit Categories - organized by reach/constraint
// =============================================================================
//
// For constrained edits (e.g., "aaa cats aaa" -> "aaa dogs aaa"), we must not
// explore edits that would delete/modify content outside the edit region.
//
// Categories are named by their "reach" - how far from cursor they affect:
//   CHAR_*      - affects single character
//   WORD_*      - affects up to word boundary
//   BIG_WORD_*  - affects up to WORD boundary (whitespace-delimited)
//   LINE_*      - affects to line boundary
//   FULL_LINE   - affects entire line
//
// Directions:
//   *_LEFT      - affects content before cursor
//   *_RIGHT     - affects content at/after cursor
//   *_END_RIGHT - affects to end of word (not start of next)
//
// =============================================================================

// Model: our strict edit section is [0, n-1]. 
// Normal and insert mode positions can range from [0, n].
// Normal:  0 1 2 3 4 5
// Insert: 0 1 2 3 4 5

// Categorization here represents how much edit scope -> corrospondingly, edits should not mess up anything.

namespace EditCategory {

// Applied in Normal mode
namespace Normal {

// Affects char to left. Cannot apply at col 0.
extern const EditToKeys CHAR_LEFT; // X

// Affects current char. Cannot apply at col n.
extern const EditToKeys CHAR_RIGHT; // x

extern const EditToKeys WORD_LEFT;  // db
extern const EditToKeys WORD_RIGHT; // dw

extern const EditToKeys WORD_END_LEFT;  // dge (delete to end of previous word)
extern const EditToKeys WORD_END_RIGHT; // de (delete to end of current word)

extern const EditToKeys BIG_WORD_LEFT;  // dB
extern const EditToKeys BIG_WORD_RIGHT; // dW

extern const EditToKeys BIG_WORD_END_RIGHT; // dE
extern const EditToKeys BIG_WORD_END_LEFT;  // dgE

// Will affect everything left in the line
extern const EditToKeys LINE_LEFT; // d^


// Will affect everything right in the line
extern const EditToKeys LINE_RIGHT; // D

extern const EditToKeys FULL_LINE;  // dd

// requires pos.line > 0
extern const EditToKeys LINE_UP; // O

// requires pos.line < n-1
extern const EditToKeys LINE_DOWN; // o

// --- Navigation motions (for EditOptimizer) ---
extern const EditToKeys NAV_VERTICAL;     // j, k
extern const EditToKeys NAV_HORIZONTAL;   // h, l
extern const EditToKeys NAV_WORD_FWD;     // w, W, e, E (forward word motions)
extern const EditToKeys NAV_WORD_BWD;     // b, B, ge, gE (backward word motions)
extern const EditToKeys NAV_LINE_START;   // 0, ^ (to line start)
extern const EditToKeys NAV_LINE_END;     // $ (to line end)

// --- Text object edits ---
extern const EditToKeys TEXT_OBJ_WORD;     // diw, daw, ciw, caw, diW, daW, ciW, caW
extern const EditToKeys TEXT_OBJ_QUOTE;    // di", da", ci", ca", di', da', ci', ca'
extern const EditToKeys TEXT_OBJ_PAREN;    // di(, da(, ci(, ca(, dib, dab, cib, cab
extern const EditToKeys TEXT_OBJ_BRACE;    // di{, da{, ci{, ca{, diB, daB, ciB, caB
extern const EditToKeys TEXT_OBJ_BRACKET;  // di[, da[, ci[, ca[
extern const EditToKeys TEXT_OBJ_ANGLE;    // di<, da<, ci<, ca<

} // namespace Normal

// =============================================================================
// Insert Mode Categories
// =============================================================================

namespace Insert {

// Affects char to left. Cannot apply at col 0.
extern const EditToKeys CHAR_LEFT;  // <BS>

// Affects char to right. Cannot apply at col n-1.
extern const EditToKeys CHAR_RIGHT; // <Del>

extern const EditToKeys WORD_LEFT;  // <C-w>

extern const EditToKeys LINE_LEFT;  // <C-u>

extern const EditToKeys LINE_UP;    // <Up>

extern const EditToKeys LINE_DOWN;  // <Down>

} // namespace Insert


extern const EditToKeys OPERATORS;  // d, c, y

} // namespace EditCategory

// All supported edit commands
extern const EditToKeys ALL_EDITS_TO_KEYS;

// =============================================================================
// Pure Deletion Operations (for EditOptimizer deletion model)
// =============================================================================
// These contain ONLY deletion commands - no mode changes, no navigation.

namespace Deletion {

extern const EditToKeys CHAR;       // x, X
extern const EditToKeys WORD;       // dw, de, db, dge, dW, dE, dB, dgE
extern const EditToKeys LINE;       // dd, D, d$, d0, d^
extern const EditToKeys TEXT_OBJ;   // diw, daw, diW, daW

} // namespace Deletion

// Check if a string is an edit
bool isEdit(std::string_view s);
