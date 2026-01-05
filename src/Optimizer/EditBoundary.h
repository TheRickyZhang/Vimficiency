#pragma once

#include <string>

// =============================================================================
// EditBoundary: Pre-computed boundary info for constrained edit operations
// =============================================================================
//
// Computed ONCE from original text when setting up an edit region.
// Used during A* search for per-position safety checking.
//
// Workflow:
// 1. Compute EditBoundary from original text (once per edit region)
// 2. During A* search, at each state:
//    - Call isForwardEditSafe/isBackwardEditSafe with current content + position
//    - Only explore edits that return true (uses hierarchy to short-circuit)
// 3. Motion application is boundary-unaware (just applies the edit)
//
// Hierarchy: CHAR < WORD < BIG_WORD < LINE
// - If boundary doesn't cut through word, ALL word-level edits are safe
// - If boundary doesn't cut through WORD, ALL WORD-level edits are safe
// - This allows early returns without computing word positions in content
//
// =============================================================================

// Forward edit categories (char < word < WORD < line hierarchy)
// "TO_START" = cursor-to-word-start motions (w, W)
// "TO_END" = cursor-to-word-end motions (e, E)
enum class ForwardEdit {
    CHAR,           // x - delete character at cursor
    WORD_TO_START,  // dw, cw - delete to next word start
    WORD_TO_END,    // de, ce - delete to word end
    BIG_WORD_TO_START, // dW, cW - delete to next WORD start
    BIG_WORD_TO_END,   // dE, cE - delete to WORD end
    LINE_TO_END     // D, C - delete to end of line
};

// Backward edit categories (char < word < WORD < line hierarchy)
enum class BackwardEdit {
    CHAR,           // X - delete char before cursor
    WORD_TO_START,  // db, cb - delete to previous word start
    WORD_TO_END,    // dge, cge - delete to previous word end
    BIG_WORD_TO_START, // dB, cB - delete to previous WORD start
    BIG_WORD_TO_END,   // dgE, cgE - delete to previous WORD end
    LINE_TO_START   // d0, d^ - delete to line start
};

struct EditBoundary {
    // Word/WORD boundary flags (does boundary cut through a word/WORD?)
    bool right_in_word = false;   // Right boundary cuts through a word
    bool right_in_WORD = false;   // Right boundary cuts through a WORD
    bool left_in_word = false;    // Left boundary cuts through a word
    bool left_in_WORD = false;    // Left boundary cuts through a WORD

    // Line boundary flags (for line-level operations)
    bool startsAtLineStart = false;  // Edit region starts at column 0
    bool endsAtLineEnd = false;      // Edit region ends at EOL
};

// =============================================================================
// Boundary Analysis (call ONCE per edit region)
// =============================================================================

// Analyze boundary from original full line.
// - fullLine: the complete original line
// - editStart: first column of edit region (inclusive)
// - editEnd: last column of edit region (inclusive)
// - startsAtLineStart: true if editStart == 0
// - endsAtLineEnd: true if editEnd == line.size() - 1
EditBoundary analyzeEditBoundary(
    const std::string& fullLine,
    int editStart,
    int editEnd,
    bool startsAtLineStart,
    bool endsAtLineEnd);

// =============================================================================
// Per-Position Safety Checking (call during A* search)
// =============================================================================
//
// These check if a specific edit at a specific position is safe.
// - editContent: CURRENT content of edit region (may differ from original)
// - cursorCol: cursor column within editContent (0-indexed)
// - boundary: EditBoundary computed once from original text
// - edit: the edit category (enum, not string)
//
// EFFICIENCY: Uses hierarchy to short-circuit.
// - If boundary doesn't cut through word, word-level checks return true immediately
// - If boundary doesn't cut through WORD, WORD-level checks return true immediately
// - Only computes word positions in content when boundary flags require it
//
// Returns true if the edit is safe (won't cross boundary).

// Forward edits: x, dw, de, dW, dE, D
bool isForwardEditSafe(
    const std::string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    ForwardEdit edit);

// Backward edits: X, db, dge, dB, dgE, d0, d^
bool isBackwardEditSafe(
    const std::string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    BackwardEdit edit);

// Full line edits: dd, cc
bool isFullLineEditSafe(const EditBoundary& boundary);
