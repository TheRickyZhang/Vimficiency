#include "ReachLevel.h"
#include "VimCore/VimUtils.h"

#include <algorithm>

using namespace std;

namespace ReachLevelUtils {

int findFirstWordEnd(const string& line) {
    int i = 0;
    int len = static_cast<int>(line.size());

    // Skip leading whitespace
    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return -1;

    // Determine word type and find its end
    if (VimUtils::isSmallWordChar(line[i])) {
        // Keyword word
        while (i < len && VimUtils::isSmallWordChar(line[i])) i++;
    } else {
        // Non-keyword, non-blank sequence (punctuation)
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

int findLastWordStart(const string& line) {
    int len = static_cast<int>(line.size());
    if (len == 0) return 0;

    int i = len - 1;

    // Skip trailing whitespace
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return len;

    // Determine word type and find its start
    if (VimUtils::isSmallWordChar(line[i])) {
        while (i > 0 && VimUtils::isSmallWordChar(line[i - 1])) i--;
    } else {
        while (i > 0 && !VimUtils::isSmallWordChar(line[i - 1]) && !VimUtils::isBlank(line[i - 1])) i--;
    }
    return i;
}

int findLastBigWordStart(const string& line) {
    int len = static_cast<int>(line.size());
    if (len == 0) return 0;

    int i = len - 1;

    // Skip trailing whitespace
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return len;

    // Find start of WORD
    while (i > 0 && !VimUtils::isBlank(line[i - 1])) i--;
    return i;
}

ReachLevel computeBackReach(int line, int col, const string& lineStr,
                            int editStartLine, ReachLevel boundaryReach) {
    // Not on the edit region's first line - can delete backwards freely
    if (line > editStartLine) {
        return ReachLevel::LINE;
    }

    // At column 0 - cannot delete backwards at all
    if (col == 0) {
        return ReachLevel::NONE;
    }

    // On first line, past col 0
    // Check what level is "free" based on position in line
    int firstWordEnd = findFirstWordEnd(lineStr);
    int firstBigWordEnd = findFirstBigWordEnd(lineStr);

    // If we're past the first WORD, BIG_WORD operations are safe
    // (they'll land within the edit region, not at col 0)
    if (col > firstBigWordEnd + 1) {
        // Past first WORD - BIG_WORD is free, LINE needs boundary permission
        return min(ReachLevel::LINE, boundaryReach);
    }

    // If we're past the first word but within first WORD
    if (col > firstWordEnd + 1) {
        // WORD is free, BIG_WORD needs permission
        return min(ReachLevel::BIG_WORD, boundaryReach);
    }

    // We're in the first word - only CHAR is free
    return min(ReachLevel::WORD, boundaryReach);
}

ReachLevel computeForwardReach(int line, int col, const string& lineStr,
                               int editEndLine, int numLines, ReachLevel boundaryReach) {
    int lineLen = static_cast<int>(lineStr.size());

    // Not on the edit region's last line - can delete forwards freely
    if (line < editEndLine) {
        return ReachLevel::LINE;
    }

    // At or past end of line - cannot delete forwards
    if (col >= lineLen) {
        return ReachLevel::NONE;
    }

    // On last line, before end
    int lastWordStart = findLastWordStart(lineStr);
    int lastBigWordStart = findLastBigWordStart(lineStr);

    // If we're before the last WORD, BIG_WORD operations are safe
    if (col < lastBigWordStart) {
        return min(ReachLevel::LINE, boundaryReach);
    }

    // If we're before the last word but within last WORD
    if (col < lastWordStart) {
        return min(ReachLevel::BIG_WORD, boundaryReach);
    }

    // We're in the last word - only CHAR is free
    return min(ReachLevel::WORD, boundaryReach);
}

} // namespace ReachLevelUtils

// =============================================================================
// Forward Reach Analysis Implementation
// =============================================================================

namespace {

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

} // anonymous namespace

// =============================================================================
// Legacy Forward Reach API Implementation
// =============================================================================

ForwardReachInfo analyzeForwardBoundary(const string& line, int editStart, int editEnd) {
    ForwardReachInfo info;
    int len = (int)line.size();

    info.editEnd = editEnd;
    info.lineLen = len;

    // Check if boundary cuts through word/WORD
    info.boundary_in_word = areInSameWord(line, editEnd, editEnd + 1);
    info.boundary_in_WORD = areInSameWORD(line, editEnd, editEnd + 1);

    // Find last word/WORD start within edit region
    info.lastWordStart = findWordStart(line, editEnd);
    info.lastBigWordStart = findBigWordStart(line, editEnd);

    // Find the safe word/WORD end (the word BEFORE the boundary-crossing word)
    // This is the end of the second-to-last word/WORD in the edit region
    if (info.boundary_in_word) {
        // Find the word before the boundary-crossing word
        // Walk backwards from lastWordStart - 1 to find the previous word
        int prevPos = info.lastWordStart - 1;
        while (prevPos >= editStart && VimUtils::isBlank(line[prevPos])) prevPos--;
        if (prevPos >= editStart) {
            info.lastSafeWordEnd = findWordEnd(line, prevPos);
        } else {
            info.lastSafeWordEnd = editStart - 1; // No safe word
        }
    } else {
        // If boundary doesn't cut through a word, the boundary char's word is safe
        info.lastSafeWordEnd = editEnd;
    }

    if (info.boundary_in_WORD) {
        // Find the WORD before the boundary-crossing WORD
        int prevPos = info.lastBigWordStart - 1;
        while (prevPos >= editStart && VimUtils::isBlank(line[prevPos])) prevPos--;
        if (prevPos >= editStart) {
            info.lastSafeBigWordEnd = findBigWordEnd(line, prevPos);
        } else {
            info.lastSafeBigWordEnd = editStart - 1; // No safe WORD
        }
    } else {
        // If boundary doesn't cut through a WORD, the boundary char's WORD is safe
        info.lastSafeBigWordEnd = editEnd;
    }

    return info;
}

bool isForwardEditSafe(const ForwardReachInfo& info, int cursorCol, const string& editType) {
    // x: always safe (deletes single char at cursor)
    if (editType == "x") {
        return cursorCol <= info.editEnd;
    }

    // D: only safe if edit ends at EOL
    if (editType == "D") {
        return info.editEnd >= info.lineLen - 1;
    }

    // dw: safe if cursor < lastWordStart OR !boundary_in_word
    if (editType == "dw") {
        if (!info.boundary_in_word) return true;
        return cursorCol < info.lastWordStart;
    }

    // de: safe if e_landing <= lastSafeWordEnd OR !boundary_in_word
    // Key insight: if cursor is AT lastSafeWordEnd (end of safe word),
    // then 'e' jumps to end of NEXT word (the boundary-crossing word) - UNSAFE!
    if (editType == "de") {
        if (!info.boundary_in_word) return true;
        // Safe if cursor is strictly before the last safe word end
        // (because 'e' from there won't reach the boundary-crossing word)
        return cursorCol < info.lastSafeWordEnd;
    }

    // dW: safe if cursor < lastBigWordStart OR !boundary_in_WORD
    if (editType == "dW") {
        if (!info.boundary_in_WORD) return true;
        return cursorCol < info.lastBigWordStart;
    }

    // dE: safe if E_landing <= lastSafeBigWordEnd OR !boundary_in_WORD
    if (editType == "dE") {
        if (!info.boundary_in_WORD) return true;
        // Similar approximation as de
        return cursorCol < info.lastBigWordStart - 1 ||
               (cursorCol < info.lastBigWordStart && cursorCol <= info.lastSafeBigWordEnd);
    }

    return false; // Unknown edit type - assume unsafe
}
