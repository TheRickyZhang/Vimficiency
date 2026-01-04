#include "BoundaryFlags.h"
#include "VimCore/VimUtils.h"

#include <algorithm>

using namespace std;

namespace {

// =============================================================================
// Helper Functions for Word/WORD Boundary Detection
// =============================================================================

// Check if two adjacent positions are in the same word
bool areInSameWord(const string& line, int pos1, int pos2) {
    if (pos1 < 0 || pos2 < 0 || pos1 >= (int)line.size() || pos2 >= (int)line.size()) {
        return false;
    }
    char c1 = line[pos1];
    char c2 = line[pos2];

    // Both must be non-blank and same word type
    if (VimUtils::isBlank(c1) || VimUtils::isBlank(c2)) return false;

    bool c1IsWordChar = VimUtils::isSmallWordChar(c1);
    bool c2IsWordChar = VimUtils::isSmallWordChar(c2);

    // Same word if both are word chars or both are non-word non-blank
    return c1IsWordChar == c2IsWordChar;
}

// Check if two adjacent positions are in the same WORD
bool areInSameWORD(const string& line, int pos1, int pos2) {
    if (pos1 < 0 || pos2 < 0 || pos1 >= (int)line.size() || pos2 >= (int)line.size()) {
        return false;
    }
    // Same WORD if neither is blank
    return !VimUtils::isBlank(line[pos1]) && !VimUtils::isBlank(line[pos2]);
}

// =============================================================================
// Word/WORD Position Finding Functions
// =============================================================================

// Find start of word containing position
int findWordStart(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;
    if (VimUtils::isBlank(line[pos])) return pos;

    int i = pos;
    if (VimUtils::isSmallWordChar(line[i])) {
        while (i > 0 && VimUtils::isSmallWordChar(line[i - 1])) i--;
    } else {
        while (i > 0 && !VimUtils::isSmallWordChar(line[i - 1]) && !VimUtils::isBlank(line[i - 1])) i--;
    }
    return i;
}

// Find end of word containing position
int findWordEnd(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;
    if (VimUtils::isBlank(line[pos])) return pos;

    int i = pos;
    int len = (int)line.size();
    if (VimUtils::isSmallWordChar(line[i])) {
        while (i + 1 < len && VimUtils::isSmallWordChar(line[i + 1])) i++;
    } else {
        while (i + 1 < len && !VimUtils::isSmallWordChar(line[i + 1]) && !VimUtils::isBlank(line[i + 1])) i++;
    }
    return i;
}

// Find start of WORD containing position
int findBigWordStart(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;
    if (VimUtils::isBlank(line[pos])) return pos;

    int i = pos;
    while (i > 0 && !VimUtils::isBlank(line[i - 1])) i--;
    return i;
}

// Find end of WORD containing position
int findBigWordEnd(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;
    if (VimUtils::isBlank(line[pos])) return pos;

    int i = pos;
    int len = (int)line.size();
    while (i + 1 < len && !VimUtils::isBlank(line[i + 1])) i++;
    return i;
}

// =============================================================================
// Forward Motion Landing Functions
// =============================================================================

// Find the end of the next word after position
int findNextWordEnd(const string& line, int pos) {
    int i = pos + 1;
    int len = (int)line.size();

    // Skip whitespace
    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return len - 1;

    return findWordEnd(line, i);
}

// Find the end of the next WORD after position
int findNextBigWordEnd(const string& line, int pos) {
    int i = pos + 1;
    int len = (int)line.size();

    // Skip whitespace
    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return len - 1;

    return findBigWordEnd(line, i);
}

// Compute where 'e' motion would land from given position
int computeELanding(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;

    // If on whitespace, go to end of next word
    if (VimUtils::isBlank(line[pos])) {
        return findNextWordEnd(line, pos);
    }

    // If at end of current word, go to end of next word
    int currentWordEnd = findWordEnd(line, pos);
    if (pos >= currentWordEnd) {
        return findNextWordEnd(line, pos);
    }

    // Otherwise, go to end of current word
    return currentWordEnd;
}

// Compute where 'E' motion would land from given position
int computeEBigLanding(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;

    // If on whitespace, go to end of next WORD
    if (VimUtils::isBlank(line[pos])) {
        return findNextBigWordEnd(line, pos);
    }

    // If at end of current WORD, go to end of next WORD
    int currentBigWordEnd = findBigWordEnd(line, pos);
    if (pos >= currentBigWordEnd) {
        return findNextBigWordEnd(line, pos);
    }

    // Otherwise, go to end of current WORD
    return currentBigWordEnd;
}

// =============================================================================
// Backward Motion Landing Functions
// =============================================================================

// Find the start of the previous word before position
int findPrevWordStart(const string& line, int pos) {
    int i = pos - 1;

    // Skip whitespace
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;

    return findWordStart(line, i);
}

// Find the start of the previous WORD before position
int findPrevBigWordStart(const string& line, int pos) {
    int i = pos - 1;

    // Skip whitespace
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;

    return findBigWordStart(line, i);
}

// Compute where 'b' motion would land from given position
int computeBLanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    // If on whitespace, go to start of previous word
    if (VimUtils::isBlank(line[pos])) {
        return findPrevWordStart(line, pos);
    }

    // If at start of current word, go to start of previous word
    int currentWordStart = findWordStart(line, pos);
    if (pos <= currentWordStart) {
        return findPrevWordStart(line, pos);
    }

    // Otherwise, go to start of current word
    return currentWordStart;
}

// Compute where 'B' motion would land from given position
int computeBBigLanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    // If on whitespace, go to start of previous WORD
    if (VimUtils::isBlank(line[pos])) {
        return findPrevBigWordStart(line, pos);
    }

    // If at start of current WORD, go to start of previous WORD
    int currentBigWordStart = findBigWordStart(line, pos);
    if (pos <= currentBigWordStart) {
        return findPrevBigWordStart(line, pos);
    }

    // Otherwise, go to start of current WORD
    return currentBigWordStart;
}

// Find the end of the previous word before position
int findPrevWordEnd(const string& line, int pos) {
    int i = pos - 1;

    // Skip whitespace
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;

    return findWordEnd(line, findWordStart(line, i));
}

// Find the end of the previous WORD before position
int findPrevBigWordEnd(const string& line, int pos) {
    int i = pos - 1;

    // Skip whitespace
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;

    return findBigWordEnd(line, findBigWordStart(line, i));
}

// Compute where 'ge' motion would land from given position
int computeGeLanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    // If on whitespace, go to end of previous word
    if (VimUtils::isBlank(line[pos])) {
        return findPrevWordEnd(line, pos);
    }

    // If at start of current word, go to end of previous word
    int currentWordStart = findWordStart(line, pos);
    if (pos <= currentWordStart) {
        return findPrevWordEnd(line, pos);
    }

    // From mid-word, go to end of previous word
    return findPrevWordEnd(line, currentWordStart);
}

// Compute where 'gE' motion would land from given position
int computeGELanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    // If on whitespace, go to end of previous WORD
    if (VimUtils::isBlank(line[pos])) {
        return findPrevBigWordEnd(line, pos);
    }

    // If at start of current WORD, go to end of previous WORD
    int currentBigWordStart = findBigWordStart(line, pos);
    if (pos <= currentBigWordStart) {
        return findPrevBigWordEnd(line, pos);
    }

    // From mid-WORD, go to end of previous WORD
    return findPrevBigWordEnd(line, currentBigWordStart);
}

// =============================================================================
// First Word/WORD End Finding (for backward motion safety)
// =============================================================================

int findFirstWordEnd(const string& line) {
    int i = 0;
    int len = static_cast<int>(line.size());

    // Skip leading whitespace
    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return -1;

    // Determine word type and find its end
    if (VimUtils::isSmallWordChar(line[i])) {
        while (i < len && VimUtils::isSmallWordChar(line[i])) i++;
    } else {
        while (i < len && !VimUtils::isSmallWordChar(line[i]) && !VimUtils::isBlank(line[i])) i++;
    }
    return i - 1;
}

int findFirstBigWordEnd(const string& line) {
    int i = 0;
    int len = static_cast<int>(line.size());

    // Skip leading whitespace
    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return -1;

    // WORD is any non-blank sequence
    while (i < len && !VimUtils::isBlank(line[i])) i++;
    return i - 1;
}

} // anonymous namespace

// =============================================================================
// Public API Implementation
// =============================================================================

BoundaryFlags analyzeBoundaryFlags(const string& fullLine, int editStart, int editEnd) {
    BoundaryFlags flags;
    // Right boundary: check if last edit char and next char are in same word/WORD
    flags.right_in_word = areInSameWord(fullLine, editEnd, editEnd + 1);
    flags.right_in_WORD = areInSameWORD(fullLine, editEnd, editEnd + 1);
    // Left boundary: check if first edit char and prev char are in same word/WORD
    flags.left_in_word = areInSameWord(fullLine, editStart - 1, editStart);
    flags.left_in_WORD = areInSameWORD(fullLine, editStart - 1, editStart);
    return flags;
}

bool isForwardEditSafeWithContent(const string& editContent, int cursorCol,
                                   const BoundaryFlags& boundary, const string& editType) {
    int len = static_cast<int>(editContent.size());
    int editEnd = len - 1;

    // x: always safe if cursor is within content
    if (editType == "x") {
        return cursorCol >= 0 && cursorCol <= editEnd;
    }

    // D: only safe if boundary is at end of line (right_in_word and right_in_WORD would be false)
    if (editType == "D") {
        return !boundary.right_in_word && !boundary.right_in_WORD;
    }

    // Find word/WORD boundaries in current edit content
    int lastWordStart = findWordStart(editContent, editEnd);
    int lastBigWordStart = findBigWordStart(editContent, editEnd);

    // dw: safe if cursor < lastWordStart OR boundary doesn't cut through word
    if (editType == "dw") {
        if (!boundary.right_in_word) return true;
        return cursorCol < lastWordStart;
    }

    // de: safe if e_landing < lastWordStart OR boundary doesn't cut through word
    if (editType == "de") {
        if (!boundary.right_in_word) return true;
        int e_landing = computeELanding(editContent, cursorCol);
        return e_landing < lastWordStart;
    }

    // dW: safe if cursor < lastBigWordStart OR boundary doesn't cut through WORD
    if (editType == "dW") {
        if (!boundary.right_in_WORD) return true;
        return cursorCol < lastBigWordStart;
    }

    // dE: safe if E_landing < lastBigWordStart OR boundary doesn't cut through WORD
    if (editType == "dE") {
        if (!boundary.right_in_WORD) return true;
        int E_landing = computeEBigLanding(editContent, cursorCol);
        return E_landing < lastBigWordStart;
    }

    return false; // Unknown edit type
}

bool isBackwardEditSafeWithContent(const string& editContent, int cursorCol,
                                    const BoundaryFlags& boundary, const string& editType) {
    int len = static_cast<int>(editContent.size());

    // Cursor must be within valid range
    if (cursorCol < 0 || cursorCol >= len) return false;

    // X: deletes char before cursor
    // Safe only if cursorCol > 0 (there's a char to delete within the region)
    if (editType == "X") {
        return cursorCol > 0;
    }

    // d0, d^: deletes to start of line/first non-blank
    // Only safe if left boundary doesn't cut through any word/WORD
    if (editType == "d0" || editType == "d^") {
        return !boundary.left_in_word && !boundary.left_in_WORD;
    }

    // Find first word/WORD end in edit content
    int firstWordEnd = findFirstWordEnd(editContent);
    int firstBigWordEnd = findFirstBigWordEnd(editContent);

    // db: deletes backward to start of previous word
    if (editType == "db") {
        if (!boundary.left_in_word) return true;
        int b_landing = computeBLanding(editContent, cursorCol);
        return b_landing > firstWordEnd;
    }

    // dge: deletes backward to end of previous word
    if (editType == "dge") {
        if (!boundary.left_in_word) return true;
        int ge_landing = computeGeLanding(editContent, cursorCol);
        return ge_landing > firstWordEnd;
    }

    // dB: deletes backward to start of previous WORD
    if (editType == "dB") {
        if (!boundary.left_in_WORD) return true;
        int B_landing = computeBBigLanding(editContent, cursorCol);
        return B_landing > firstBigWordEnd;
    }

    // dgE: deletes backward to end of previous WORD
    if (editType == "dgE") {
        if (!boundary.left_in_WORD) return true;
        int gE_landing = computeGELanding(editContent, cursorCol);
        return gE_landing > firstBigWordEnd;
    }

    return false; // Unknown edit type
}
