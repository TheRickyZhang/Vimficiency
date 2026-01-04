#pragma once

#include <string>

// =============================================================================
// Boundary Flags for Constrained Edit Operations
// =============================================================================
//
// When editing a substring within a line, we need to know which edit operations
// are safe at each cursor position. An edit is "safe" if it doesn't modify
// characters outside the edit boundary.
//
// KEY INSIGHT: The boundary analysis is done ONCE on the original text, but
// the edit content changes during A* search. We separate:
// 1. BoundaryFlags: computed once from original text (does boundary cut through word/WORD?)
// 2. Safety check functions: computed per-state using current edit content + boundary flags
//
// Example: Original "abc def.gh i", edit bounds [1,8] ("bc def.g")
// - Boundary analysis: g and h are same word → right_in_word = true
// - After edit content becomes "x yz", last word "yz" connects to "h" → "yzh"
// - Forward edits from "yz" would delete into "h", which is outside boundary
//

// Boundary flags computed once from original text
struct BoundaryFlags {
    bool right_in_word;   // Right boundary cuts through a word (last edit char + next char are same word)
    bool right_in_WORD;   // Right boundary cuts through a WORD
    bool left_in_word;    // Left boundary cuts through a word (first edit char + prev char are same word)
    bool left_in_WORD;    // Left boundary cuts through a WORD
};

// Analyze boundary flags from original text.
// Call this ONCE when setting up the edit region.
// - editStart: first column of edit region (inclusive)
// - editEnd: last column of edit region (inclusive)
BoundaryFlags analyzeBoundaryFlags(const std::string& fullLine, int editStart, int editEnd);

// Check if a forward edit is safe using current edit content + boundary flags.
// - editContent: the CURRENT content of the edit region (may differ from original)
// - cursorCol: cursor column within editContent (0-indexed)
// - boundary: BoundaryFlags computed once from original text
// - editType: one of "x", "dw", "de", "dW", "dE", "D"
bool isForwardEditSafeWithContent(const std::string& editContent, int cursorCol,
                                   const BoundaryFlags& boundary, const std::string& editType);

// Check if a backward edit is safe using current edit content + boundary flags.
// - editContent: the CURRENT content of the edit region
// - cursorCol: cursor column within editContent (0-indexed)
// - boundary: BoundaryFlags computed once from original text
// - editType: one of "X", "db", "dge", "dB", "dgE", "d0", "d^"
bool isBackwardEditSafeWithContent(const std::string& editContent, int cursorCol,
                                    const BoundaryFlags& boundary, const std::string& editType);
