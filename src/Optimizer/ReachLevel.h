#pragma once

#include <string>

// Total ordering of edit reach levels (from most restrictive to least)
// Each level includes all levels below it.
enum class ReachLevel {
    NONE = 0,      // Cannot delete in this direction
    CHAR = 1,      // Can delete single character (X, x, <BS>, <Del>)
    WORD = 2,      // Can delete up to word boundary (db, dw, <C-w>)
    BIG_WORD = 3,  // Can delete up to WORD boundary (dB, dW)
    LINE = 4,      // Can delete to line boundary (d0, d^, D, C, <C-u>)
};

// Comparison operators for ReachLevel
inline bool operator<(ReachLevel a, ReachLevel b) {
    return static_cast<int>(a) < static_cast<int>(b);
}
inline bool operator<=(ReachLevel a, ReachLevel b) {
    return static_cast<int>(a) <= static_cast<int>(b);
}
inline bool operator>(ReachLevel a, ReachLevel b) {
    return static_cast<int>(a) > static_cast<int>(b);
}
inline bool operator>=(ReachLevel a, ReachLevel b) {
    return static_cast<int>(a) >= static_cast<int>(b);
}

namespace ReachLevelUtils {

// Find the end column (inclusive) of the first word in the line.
// Returns -1 if no word exists.
// A "word" is a sequence of isSmallWordChar, or a sequence of non-word non-blank chars.
int findFirstWordEnd(const std::string& line);

// Find the end column (inclusive) of the first WORD in the line.
// Returns -1 if no WORD exists.
// A "WORD" is any sequence of non-blank characters.
int findFirstBigWordEnd(const std::string& line);

// Find the start column of the last word in the line.
// Returns line.size() if no word exists.
int findLastWordStart(const std::string& line);

// Find the start column of the last WORD in the line.
// Returns line.size() if no WORD exists.
int findLastBigWordStart(const std::string& line);

// Compute the effective backwards reach at position (line, col).
// - editStartLine: the first line of the edit region (usually 0)
// - boundaryReach: what the edit region's left boundary allows
ReachLevel computeBackReach(int line, int col, const std::string& lineStr,
                            int editStartLine, ReachLevel boundaryReach);

// Compute the effective forwards reach at position (line, col).
// - editEndLine: the last line of the edit region
// - numLines: total lines in the edit region
// - boundaryReach: what the edit region's right boundary allows
ReachLevel computeForwardReach(int line, int col, const std::string& lineStr,
                               int editEndLine, int numLines, ReachLevel boundaryReach);

} // namespace ReachLevelUtils

// =============================================================================
// Legacy Forward Reach API (for backwards compatibility with existing tests)
// =============================================================================
//
// NOTE: For new code, use BoundaryFlags from Optimizer/BoundaryFlags.h instead.
// The BoundaryFlags API separates boundary analysis (done once) from safety
// checks (done per-state with potentially changed content during A* search).
//

struct ForwardReachInfo {
    bool boundary_in_word;      // Last edit char in same word as next char?
    bool boundary_in_WORD;      // Last edit char in same WORD as next char?
    int lastWordStart;          // Start column of last word in edit region
    int lastBigWordStart;       // Start column of last WORD in edit region
    int lastSafeWordEnd;        // End column of word BEFORE the boundary-crossing word
    int lastSafeBigWordEnd;     // End column of WORD BEFORE the boundary-crossing WORD
    int editEnd;                // End column of the edit region (inclusive)
    int lineLen;                // Total length of the line
};

// Analyze the forward boundary of an edit region (legacy API for full line).
ForwardReachInfo analyzeForwardBoundary(const std::string& line, int editStart, int editEnd);

// Check if a forward edit is safe (legacy API using full line info).
bool isForwardEditSafe(const ForwardReachInfo& info, int cursorCol, const std::string& editType);
