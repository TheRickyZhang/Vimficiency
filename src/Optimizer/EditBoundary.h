#pragma once

#include <cstdint>
#include <string>

// =============================================================================
// EditBoundary: Pre-computed boundary info for constrained edit operations
// =============================================================================
//
// Stores the CharType of characters adjacent to the edit region boundaries.
// During A* search, we check if a motion would "cross" the boundary by
// looking up (lastChar/firstChar, boundaryChar) in motion-specific tables.
//
// Workflow:
// 1. Compute EditBoundary from original text (once per edit region)
// 2. During A* search, compute lastChar/firstChar from current content
// 3. Use canXxxCross() to determine if motion would escape the boundary
//
// See data/EditBoundaryLogic.typ for the complete crossing logic tables.
//
// =============================================================================

// Character classification for boundary crossing logic
enum class CharType : uint8_t {
    Keyword,     // alphanumeric + underscore (vim 'iskeyword')
    Whitespace,  // space, tab, etc.
    Symbol,      // punctuation and other non-word chars
    Newline      // at line boundary (nothing beyond)
};

// =============================================================================
// Edit operation categories (used by EditOptimizer for operation dispatch)
// =============================================================================

// Forward edit categories
enum class ForwardEdit {
    CHAR,              // x - delete character at cursor
    WORD_TO_START,     // dw, cw - delete to next word start
    WORD_TO_END,       // de, ce - delete to word end
    BIG_WORD_TO_START, // dW, cW - delete to next WORD start
    BIG_WORD_TO_END,   // dE, cE - delete to WORD end
    LINE_TO_END        // D, C - delete to end of line
};

// Backward edit categories
enum class BackwardEdit {
    CHAR,              // X - delete char before cursor
    WORD_TO_START,     // db, cb - delete to previous word start
    WORD_TO_END,       // dge, cge - delete to previous word end
    BIG_WORD_TO_START, // dB, cB - delete to previous WORD start
    BIG_WORD_TO_END,   // dgE, cgE - delete to previous WORD end
    LINE_TO_START      // d0, d^ - delete to line start
};

// Get CharType for a character
CharType getCharType(char c);

// =============================================================================
// EditBoundary struct
// =============================================================================

struct EditBoundary {
    // Boundary character types (what's OUTSIDE the edit region)
    CharType rightBoundaryChar = CharType::Newline;  // char after edit region end
    CharType leftBoundaryChar = CharType::Newline;   // char before edit region start

    // Context flags for line-level operations (dd, cc)
    bool hasLinesAbove = false;  // lines exist above edit region
    bool hasLinesBelow = false;  // lines exist below edit region

    // Convenience checks
    bool atLineEnd() const { return rightBoundaryChar == CharType::Newline; }
    bool atLineStart() const { return leftBoundaryChar == CharType::Newline; }
};

// =============================================================================
// Boundary crossing checks
// =============================================================================
//
// These determine if a motion CAN cross the boundary, based on:
// - lastChar/firstChar: CharType of the last/first char in current edit content
// - boundaryChar: CharType stored in EditBoundary (what's outside)
//
// Returns true if motion WOULD cross (unsafe), false if motion is safe.
//
// Forward motions use (lastChar, rightBoundaryChar)
// Backward motions use (firstChar, leftBoundaryChar)

// Forward word motions
bool canDwCross(CharType lastChar, CharType boundaryChar);   // dw, cw
bool canDeCross(CharType lastChar, CharType boundaryChar);   // de, ce

// Forward WORD motions
bool canDWCross(CharType lastChar, CharType boundaryChar);   // dW, cW
bool canDECross(CharType lastChar, CharType boundaryChar);   // dE, cE

// Backward word motions
bool canDbCross(CharType firstChar, CharType boundaryChar);  // db, cb
bool canDgeCross(CharType firstChar, CharType boundaryChar); // dge, cge

// Backward WORD motions
bool canDBCross(CharType firstChar, CharType boundaryChar);  // dB, cB
bool canDgECross(CharType firstChar, CharType boundaryChar); // dgE, cgE

// =============================================================================
// Line-level operations
// =============================================================================

// Full line edit (dd, cc, S) - safe only if edit spans entire line(s)
bool isFullLineEditSafe(const EditBoundary& boundary);

// =============================================================================
// Boundary construction
// =============================================================================

// Analyze boundary from original full line.
// - fullLine: the complete original line
// - editStart: first column of edit region (inclusive)
// - editEnd: last column of edit region (inclusive)
EditBoundary analyzeEditBoundary(
    const std::string& fullLine,
    int editStart,
    int editEnd);

// =============================================================================
// TEMPORARY: Legacy API stubs (to be removed when EditOptimizer is updated)
// =============================================================================

// TODO: Remove these once EditOptimizer uses the new canXxxCross() functions
bool isForwardEditSafe(
    const std::string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    ForwardEdit edit);

bool isBackwardEditSafe(
    const std::string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    BackwardEdit edit);
