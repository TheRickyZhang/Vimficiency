#include "Boundary.h"
#include "VimCore/VimMovementUtils.h"

// =============================================================================
// Helper: Get char type at position
// =============================================================================
//
// Precondition: position is valid (lines non-empty, line non-empty, col in range)
// Exception: returns Newline for boundary positions at line edges

CharType getCharTypeAt(const Lines& lines, Position pos) {
    if (lines.empty() || lines[pos.line].empty()) return CharType::Newline;
    return getCharType(lines[pos.line][pos.col]);
}

CharType getCharTypeBefore(const Lines& lines, Position pos) {
    if (pos.col > 0) {
        return getCharType(lines[pos.line][pos.col - 1]);
    }
    // At column 0: go to previous line's last char
    for (int prevLine = pos.line - 1; prevLine >= 0; --prevLine) {
        if (!lines[prevLine].empty()) {
            return getCharType(lines[prevLine].back());
        }
    }
    return CharType::Newline;
}

CharType getCharTypeAfter(const Lines& lines, Position pos) {
    const std::string& line = lines[pos.line];
    if (pos.col + 1 < static_cast<int>(line.size())) {
        return getCharType(line[pos.col + 1]);
    }
    // At end of line: go to next line's first char
    for (int nextLine = pos.line + 1; nextLine < static_cast<int>(lines.size()); ++nextLine) {
        if (!lines[nextLine].empty()) {
            return getCharType(lines[nextLine][0]);
        }
    }
    return CharType::Newline;
}

// =============================================================================
// Core API
// =============================================================================

bool extendsTooFar(
    const Lines& lines,
    Position cursor,
    Position boundaryPos,
    const MotionInfo& info) {

    Position endPos = cursor;

    // Use the appropriate motion wrapper which handles edge cases correctly.
    // Forward motions check if endPos >= boundaryPos.
    // Backward motions check if endPos <= boundaryPos.
    //
    // Motion wrappers used:
    //   de/dE: motionE (steps forward first, then finds word end)
    //   dw/dW: motionW (finds next word start, but deletion uses Space endpoint)
    //   db/dB: motionB (finds previous word start)
    //   dge/dgE: motionGe (steps backward first, then finds word end)

    if (info.isForward) {
        if (info.endpointType == EndpointType::End) {
            // de/dE: use motionE which steps forward first
            VimMovementUtils::motionE(endPos, lines, info.isWORD);
        } else {
            // dw/dW: use motionWord with Space endpoint
            VimMovementUtils::motionWord(endPos, lines, true, EndpointType::Space, info.isWORD);
        }
        return endPos >= boundaryPos;
    } else {
        if (info.endpointType == EndpointType::End) {
            // db/dB: use motionB (finds previous word START)
            // The deletion range is [motionB_result, cursor-1]
            VimMovementUtils::motionB(endPos, lines, info.isWORD);
        } else {
            // dge/dgE: use motionGe which steps backward first
            VimMovementUtils::motionGe(endPos, lines, info.isWORD);
        }
        return endPos <= boundaryPos;
    }
}
