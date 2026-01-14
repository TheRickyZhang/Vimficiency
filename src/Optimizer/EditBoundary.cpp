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
// Forward word motion crossing checks
// See data/EditBoundaryLogic.typ for derivation
// =============================================================================

// dw: delete to next word start (includes trailing whitespace)
//
//               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// --------------+-------------------+-------------------+-------------------+
// last=keyword  |  YES              |  YES              |  no               |
// last=space    |  no               |  YES              |  no               |
// last=symbol   |  no               |  no               |  no               |
//
bool canDwCross(CharType lastChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (lastChar == CharType::Symbol) return false;
    if (lastChar == CharType::Keyword) return bc != CharType::Symbol;
    // lastChar == Whitespace
    return bc == CharType::Whitespace;
}

// de: delete to word end
//
//               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// --------------+-------------------+-------------------+-------------------+
// last=keyword  |  YES              |  no               |  no               |
// last=space    |  no               |  no               |  no               |
// last=symbol   |  no               |  no               |  no               |
//
bool canDeCross(CharType lastChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    return lastChar == CharType::Keyword && bc == CharType::Keyword;
}

// =============================================================================
// Forward WORD motion crossing checks
// =============================================================================

// dW: delete to next WORD start (includes trailing whitespace)
//
//               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// --------------+-------------------+-------------------+-------------------+
// last=keyword  |  YES              |  YES              |  YES              |
// last=space    |  no               |  YES              |  no               |
// last=symbol   |  YES              |  YES              |  YES              |
//
bool canDWCross(CharType lastChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (lastChar == CharType::Whitespace) return bc == CharType::Whitespace;
    // lastChar == Keyword or Symbol: always crosses (WORD chars)
    return true;
}

// dE: delete to WORD end
//
//               |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// --------------+-------------------+-------------------+-------------------+
// last=keyword  |  YES              |  no               |  YES              |
// last=space    |  no               |  no               |  no               |
// last=symbol   |  YES              |  no               |  YES              |
//
bool canDECross(CharType lastChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (lastChar == CharType::Whitespace) return false;
    // lastChar == Keyword or Symbol: crosses if bc is also non-whitespace
    return bc != CharType::Whitespace;
}

// =============================================================================
// Backward word motion crossing checks
// =============================================================================

// db: delete backward to word start
//
//                |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// ---------------+-------------------+-------------------+-------------------+
// first=keyword  |  YES              |  no               |  no               |
// first=space    |  no               |  no               |  no               |
// first=symbol   |  no               |  no               |  no               |
//
bool canDbCross(CharType firstChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    return firstChar == CharType::Keyword && bc == CharType::Keyword;
}

// dge: delete backward to previous word end
//
//                |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// ---------------+-------------------+-------------------+-------------------+
// first=keyword  |  YES              |  YES              |  YES              |
// first=space    |  YES              |  YES              |  YES              |
// first=symbol   |  no               |  no               |  no               |
//
bool canDgeCross(CharType firstChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    return firstChar != CharType::Symbol;
}

// =============================================================================
// Backward WORD motion crossing checks
// =============================================================================

// dB: delete backward to WORD start
//
//                |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// ---------------+-------------------+-------------------+-------------------+
// first=keyword  |  YES              |  no               |  YES              |
// first=space    |  no               |  no               |  no               |
// first=symbol   |  YES              |  no               |  YES              |
//
bool canDBCross(CharType firstChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (firstChar == CharType::Whitespace) return false;
    // firstChar == Keyword or Symbol: crosses if bc is also non-whitespace
    return bc != CharType::Whitespace;
}

// dgE: delete backward to previous WORD end
//
//                |  bc=keyword       |  bc=whitespace    |  bc=symbol        |
// ---------------+-------------------+-------------------+-------------------+
// first=keyword  |  YES              |  YES              |  YES              |
// first=space    |  YES              |  YES              |  YES              |
// first=symbol   |  YES              |  no               |  YES              |
//
bool canDgECross(CharType firstChar, CharType bc) {
    if (bc == CharType::Newline) return false;
    if (firstChar == CharType::Whitespace) return false;
    if (firstChar == CharType::Symbol) return bc == CharType::Symbol;
    // firstChar == Keyword: always crosses
    return true;
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

// =============================================================================
// TEMPORARY: Legacy API stubs
// TODO: Remove these once EditOptimizer is updated to use canXxxCross()
// =============================================================================

bool isForwardEditSafe(
    const std::string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    ForwardEdit edit) {

    if (editContent.empty()) return false;
    CharType lastChar = getCharType(editContent.back());

    switch (edit) {
        case ForwardEdit::CHAR:
            return cursorCol >= 0 && cursorCol < static_cast<int>(editContent.size());
        case ForwardEdit::LINE_TO_END:
            return boundary.atLineEnd();
        case ForwardEdit::WORD_TO_START:
            return !canDwCross(lastChar, boundary.rightBoundaryChar);
        case ForwardEdit::WORD_TO_END:
            return !canDeCross(lastChar, boundary.rightBoundaryChar);
        case ForwardEdit::BIG_WORD_TO_START:
            return !canDWCross(lastChar, boundary.rightBoundaryChar);
        case ForwardEdit::BIG_WORD_TO_END:
            return !canDECross(lastChar, boundary.rightBoundaryChar);
    }
    return false;
}

bool isBackwardEditSafe(
    const std::string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    BackwardEdit edit) {

    if (editContent.empty()) return false;
    CharType firstChar = getCharType(editContent.front());

    switch (edit) {
        case BackwardEdit::CHAR:
            return cursorCol > 0;
        case BackwardEdit::LINE_TO_START:
            return boundary.atLineStart();
        case BackwardEdit::WORD_TO_START:
            return !canDbCross(firstChar, boundary.leftBoundaryChar);
        case BackwardEdit::WORD_TO_END:
            return !canDgeCross(firstChar, boundary.leftBoundaryChar);
        case BackwardEdit::BIG_WORD_TO_START:
            return !canDBCross(firstChar, boundary.leftBoundaryChar);
        case BackwardEdit::BIG_WORD_TO_END:
            return !canDgECross(firstChar, boundary.leftBoundaryChar);
    }
    return false;
}
