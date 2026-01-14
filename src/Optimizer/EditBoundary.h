#pragma once

#include <cstdint>
#include <string>

// =============================================================================
// EditBoundary: Pre-computed boundary info for constrained edit operations
// =============================================================================
//
// See data/EditBoundaryLogic.typ for the complete crossing logic.
//
// Endpoint Types (what a deletion stops at):
//   End:   Last char of word
//   Space: Char before next word (includes trailing whitespace)
//   Next:  Start of next word (crosses into adjacent word)
//   Line:  End of line
//
// Workflow:
// 1. Compute EditBoundary from original text (once per edit region)
// 2. During A* search, compute lastChar/firstChar from current content
// 3. Use canXxxCross() to determine if motion would escape the boundary
//
// =============================================================================

// Character classification for boundary crossing logic
enum class CharType : uint8_t {
    Keyword,     // alphanumeric + underscore (vim 'iskeyword')
    Whitespace,  // space, tab, etc.
    Symbol,      // punctuation and other non-word chars
    Newline      // at line boundary (nothing beyond)
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
// Endpoint-based crossing checks (word)
// =============================================================================
//
// Returns true if motion WOULD cross (unsafe), false if safe.
// Forward: check (lastChar, rightBoundaryChar)
// Backward: check (firstChar, leftBoundaryChar)

// End: stops at word boundary (de, db, diw edges)
bool canEndCross(CharType c, CharType bc);

// Space: stops at word end + trailing whitespace (dw, daw trailing edge)
bool canSpaceCross(CharType c, CharType bc);

// Next: crosses into adjacent word (dge)
bool canNextCross(CharType c, CharType bc);

// Line: crosses to line boundary (D, C, d$, d0)
bool canLineCross(CharType bc);

// =============================================================================
// Endpoint-based crossing checks (WORD)
// =============================================================================
//
// Same as word versions, but Keyword and Symbol merge into NonWhitespace.

// END: stops at WORD boundary (dE, dB, diW edges)
bool canEndCrossWORD(CharType c, CharType bc);

// SPACE: stops at WORD end + trailing whitespace (dW, daW trailing edge)
bool canSpaceCrossWORD(CharType c, CharType bc);

// NEXT: crosses into adjacent WORD (dgE)
bool canNextCrossWORD(CharType c, CharType bc);

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
