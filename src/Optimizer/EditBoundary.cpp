#include "EditBoundary.h"
#include "VimCore/VimUtils.h"

// =============================================================================
// Character classification
// =============================================================================

CharType getCharType(char c) {
    if (VimUtils::isSmallWordChar(c)) return CharType::Keyword;
    if (VimUtils::isBlank(c)) return CharType::Whitespace;
    return CharType::Symbol;
}

// =============================================================================
// Endpoint-based crossing checks (word)
// See data/EditBoundaryLogic.typ for derivation
// =============================================================================

// End: stops at word boundary (de, db motions)
//
// Uses wordChar/nonWordChar concept: motion continues through same word type.
// Whitespace isn't a word, so `e` from whitespace goes to NEXT word end.
//
//               |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
// --------------+--------------+-----------------+-------------+--------------+
// char=Keyword  |  YES         |  no             |  no         |  no          |
// char=Space    |  YES         |  YES            |  YES        |  no          |
// char=Symbol   |  no          |  no             |  YES        |  no          |
//
bool canEndCross(CharType c, CharType bc) {
    if (bc == CharType::Newline) return false;
    // Whitespace isn't a word - `e` goes to NEXT word end, crossing everything
    if (c == CharType::Whitespace) return true;
    // Same word type continues: Keyword→Keyword or Symbol→Symbol
    return c == bc;
}

// Space: stops at word end + trailing whitespace (dw, db motions)
//
// Uses wordChar/nonWordChar concept: motion continues through same word type + whitespace.
// - For Keyword content: crosses Keyword and Whitespace, stops at Symbol
// - For Symbol content: crosses Symbol and Whitespace, stops at Keyword
//
//               |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
// --------------+--------------+-----------------+-------------+--------------+
// char=Keyword  |  YES         |  YES            |  no         |  no          |
// char=Space    |  no          |  YES            |  no         |  no          |
// char=Symbol   |  no          |  YES            |  YES        |  no          |
//
bool canSpaceCross(CharType c, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (c == CharType::Whitespace) return bc == CharType::Whitespace;
    // For Keyword/Symbol: crosses same type and whitespace, stops at different type
    if (bc == CharType::Whitespace) return true;
    return c == bc;  // Same word type continues
}

// Next: crosses into adjacent word (dge motion)
//
// Uses wordChar/nonWordChar concept applied symmetrically.
// Original table had Keyword/Whitespace crossing everything, Symbol crossing nothing.
// With symmetric interpretation, Symbol should also cross everything.
//
//               |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
// --------------+--------------+-----------------+-------------+--------------+
// char=Keyword  |  YES         |  YES            |  YES        |  no          |
// char=Space    |  YES         |  YES            |  YES        |  no          |
// char=Symbol   |  YES         |  YES            |  YES        |  no          |
//
bool canNextCross(CharType c, CharType bc) {
    (void)c;  // All word types cross - ge always goes to previous word end
    return bc != CharType::Newline;
}

// Line: crosses to line boundary
//
//               |  bc=Keyword  |  bc=Whitespace  |  bc=Symbol  |  bc=Newline  |
// --------------+--------------+-----------------+-------------+--------------+
//               |  YES         |  YES            |  YES        |  no          |
//
bool canLineCross(CharType bc) {
    return bc != CharType::Newline;
}

// =============================================================================
// Endpoint-based crossing checks (WORD)
// Keyword and Symbol merge into NonWhitespace
// =============================================================================

// END: stops at WORD boundary (dE, dB motions)
//
// For WORD motions, Keyword and Symbol merge into NonWhitespace.
// Whitespace isn't a WORD, so `E` from whitespace goes to NEXT WORD end.
//
//               |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
// --------------+------------+-----------------+--------------+
// char=WORD     |  YES       |  no             |  no          |
// char=Space    |  YES       |  YES            |  no          |
//
bool canEndCrossWORD(CharType c, CharType bc) {
    if (bc == CharType::Newline) return false;
    // Whitespace isn't a WORD - `E` goes to NEXT WORD end, crossing everything
    if (c == CharType::Whitespace) return true;
    // NonWS (Keyword or Symbol) continues through non-whitespace boundary
    return bc != CharType::Whitespace;
}

// SPACE: stops at WORD end + trailing whitespace
//
//               |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
// --------------+------------+-----------------+--------------+
// char=WORD     |  YES       |  YES            |  no          |
// char=Space    |  no        |  YES            |  no          |
//
bool canSpaceCrossWORD(CharType c, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (c == CharType::Whitespace) return bc == CharType::Whitespace;
    // c is NonWS: always crosses except to Newline
    return true;
}

// NEXT: crosses into adjacent WORD
//
//               |  bc=NonWS  |  bc=Whitespace  |  bc=Newline  |
// --------------+------------+-----------------+--------------+
// char=WORD     |  YES       |  YES            |  no          |
// char=Space    |  YES       |  YES            |  no          |
//
bool canNextCrossWORD(CharType c, CharType bc) {
    if (bc == CharType::Newline) return false;
    // For WORD, everything except Newline allows crossing
    return c != CharType::Whitespace || bc != CharType::Newline;
    // Simplified: always true when bc != Newline (already checked above)
}

// =============================================================================
// Line-level operations
// =============================================================================

bool isFullLineEditSafe(const EditBoundary& boundary) {
    return boundary.atLineStart() && boundary.atLineEnd();
}

// =============================================================================
// Boundary construction
// =============================================================================

EditBoundary analyzeEditBoundary(
    const std::string& fullLine,
    int editStart,
    int editEnd) {

    EditBoundary b;
    int len = static_cast<int>(fullLine.size());

    // Right boundary: what's after the edit region?
    if (editEnd + 1 < len) {
        b.rightBoundaryChar = getCharType(fullLine[editEnd + 1]);
    } else {
        b.rightBoundaryChar = CharType::Newline;
    }

    // Left boundary: what's before the edit region?
    if (editStart > 0) {
        b.leftBoundaryChar = getCharType(fullLine[editStart - 1]);
    } else {
        b.leftBoundaryChar = CharType::Newline;
    }

    // hasLinesAbove/hasLinesBelow must be set by caller with multi-line context

    return b;
}
