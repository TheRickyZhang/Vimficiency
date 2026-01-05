#include "EditBoundary.h"
#include "VimCore/VimUtils.h"
#include "Utils/Debug.h"

using namespace std;

// =============================================================================
// Helper Functions
// =============================================================================

namespace {

// Check if two adjacent positions are in the same word
bool areInSameWord(const string& line, int pos1, int pos2) {
    if (pos1 < 0 || pos2 < 0 || pos1 >= (int)line.size() || pos2 >= (int)line.size()) {
        return false;
    }
    char c1 = line[pos1];
    char c2 = line[pos2];

    if (VimUtils::isBlank(c1) || VimUtils::isBlank(c2)) return false;

    bool c1IsWordChar = VimUtils::isSmallWordChar(c1);
    bool c2IsWordChar = VimUtils::isSmallWordChar(c2);

    return c1IsWordChar == c2IsWordChar;
}

// Check if two adjacent positions are in the same WORD
bool areInSameWORD(const string& line, int pos1, int pos2) {
    if (pos1 < 0 || pos2 < 0 || pos1 >= (int)line.size() || pos2 >= (int)line.size()) {
        return false;
    }
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

// Find end of next word after position
int findNextWordEnd(const string& line, int pos) {
    int i = pos + 1;
    int len = (int)line.size();

    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return len - 1;

    return findWordEnd(line, i);
}

// Find end of next WORD after position
int findNextBigWordEnd(const string& line, int pos) {
    int i = pos + 1;
    int len = (int)line.size();

    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return len - 1;

    return findBigWordEnd(line, i);
}

// Compute where 'e' motion would land
int computeELanding(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;

    if (VimUtils::isBlank(line[pos])) {
        return findNextWordEnd(line, pos);
    }

    int currentWordEnd = findWordEnd(line, pos);
    if (pos >= currentWordEnd) {
        return findNextWordEnd(line, pos);
    }

    return currentWordEnd;
}

// Compute where 'E' motion would land
int computeEBigLanding(const string& line, int pos) {
    if (pos < 0 || pos >= (int)line.size()) return pos;

    if (VimUtils::isBlank(line[pos])) {
        return findNextBigWordEnd(line, pos);
    }

    int currentBigWordEnd = findBigWordEnd(line, pos);
    if (pos >= currentBigWordEnd) {
        return findNextBigWordEnd(line, pos);
    }

    return currentBigWordEnd;
}

// Find start of previous word
int findPrevWordStart(const string& line, int pos) {
    int i = pos - 1;
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;
    return findWordStart(line, i);
}

// Find start of previous WORD
int findPrevBigWordStart(const string& line, int pos) {
    int i = pos - 1;
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;
    return findBigWordStart(line, i);
}

// Compute where 'b' motion would land
int computeBLanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    if (VimUtils::isBlank(line[pos])) {
        return findPrevWordStart(line, pos);
    }

    int currentWordStart = findWordStart(line, pos);
    if (pos <= currentWordStart) {
        return findPrevWordStart(line, pos);
    }

    return currentWordStart;
}

// Compute where 'B' motion would land
int computeBBigLanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    if (VimUtils::isBlank(line[pos])) {
        return findPrevBigWordStart(line, pos);
    }

    int currentBigWordStart = findBigWordStart(line, pos);
    if (pos <= currentBigWordStart) {
        return findPrevBigWordStart(line, pos);
    }

    return currentBigWordStart;
}

// Find end of previous word
int findPrevWordEnd(const string& line, int pos) {
    int i = pos - 1;
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;
    return findWordEnd(line, findWordStart(line, i));
}

// Find end of previous WORD
int findPrevBigWordEnd(const string& line, int pos) {
    int i = pos - 1;
    while (i >= 0 && VimUtils::isBlank(line[i])) i--;
    if (i < 0) return 0;
    return findBigWordEnd(line, findBigWordStart(line, i));
}

// Compute where 'ge' motion would land
int computeGeLanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    if (VimUtils::isBlank(line[pos])) {
        return findPrevWordEnd(line, pos);
    }

    int currentWordStart = findWordStart(line, pos);
    if (pos <= currentWordStart) {
        return findPrevWordEnd(line, pos);
    }

    return findPrevWordEnd(line, currentWordStart);
}

// Compute where 'gE' motion would land
int computeGELanding(const string& line, int pos) {
    if (pos <= 0) return 0;

    if (VimUtils::isBlank(line[pos])) {
        return findPrevBigWordEnd(line, pos);
    }

    int currentBigWordStart = findBigWordStart(line, pos);
    if (pos <= currentBigWordStart) {
        return findPrevBigWordEnd(line, pos);
    }

    return findPrevBigWordEnd(line, currentBigWordStart);
}

// Find first word end in content
int findFirstWordEnd(const string& line) {
    int i = 0;
    int len = static_cast<int>(line.size());

    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return -1;

    if (VimUtils::isSmallWordChar(line[i])) {
        while (i < len && VimUtils::isSmallWordChar(line[i])) i++;
    } else {
        while (i < len && !VimUtils::isSmallWordChar(line[i]) && !VimUtils::isBlank(line[i])) i++;
    }
    return i - 1;
}

// Find first WORD end in content
int findFirstBigWordEnd(const string& line) {
    int i = 0;
    int len = static_cast<int>(line.size());

    while (i < len && VimUtils::isBlank(line[i])) i++;
    if (i >= len) return -1;

    while (i < len && !VimUtils::isBlank(line[i])) i++;
    return i - 1;
}

} // anonymous namespace

// =============================================================================
// Public API
// =============================================================================

EditBoundary analyzeEditBoundary(
    const string& fullLine,
    int editStart,
    int editEnd,
    bool startsAtLineStart,
    bool endsAtLineEnd) {

    EditBoundary boundary;

    // Word/WORD boundary analysis
    boundary.right_in_word = areInSameWord(fullLine, editEnd, editEnd + 1);
    boundary.right_in_WORD = areInSameWORD(fullLine, editEnd, editEnd + 1);
    boundary.left_in_word = areInSameWord(fullLine, editStart - 1, editStart);
    boundary.left_in_WORD = areInSameWORD(fullLine, editStart - 1, editStart);

    // Line boundary info
    boundary.startsAtLineStart = startsAtLineStart;
    boundary.endsAtLineEnd = endsAtLineEnd;

    return boundary;
}

bool isForwardEditSafe(
    const string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    ForwardEdit edit) {

    int len = static_cast<int>(editContent.size());
    int editEnd = len - 1;

    switch (edit) {
        case ForwardEdit::CHAR:
            // x: safe if cursor is within content
            return cursorCol >= 0 && cursorCol <= editEnd;

        case ForwardEdit::LINE_TO_END:
            // D: only safe if edit region ends at EOL
            return boundary.endsAtLineEnd;

        case ForwardEdit::WORD_TO_START:
            // dw: safe if boundary doesn't cut through word, OR cursor < lastWordStart
            if (!boundary.right_in_word) return true;
            return cursorCol < findWordStart(editContent, editEnd);

        case ForwardEdit::WORD_TO_END:
            // de: safe if boundary doesn't cut through word, OR e_landing < lastWordStart
            if (!boundary.right_in_word) return true;
            return computeELanding(editContent, cursorCol) < findWordStart(editContent, editEnd);

        case ForwardEdit::BIG_WORD_TO_START:
            // dW: safe if boundary doesn't cut through WORD, OR cursor < lastBigWordStart
            if (!boundary.right_in_WORD) return true;
            return cursorCol < findBigWordStart(editContent, editEnd);

        case ForwardEdit::BIG_WORD_TO_END:
            // dE: safe if boundary doesn't cut through WORD, OR E_landing < lastBigWordStart
            if (!boundary.right_in_WORD) return true;
            return computeEBigLanding(editContent, cursorCol) < findBigWordStart(editContent, editEnd);
    }

    debug("should not reach here?");
    return false; // Should not reach here
}

bool isBackwardEditSafe(
    const string& editContent,
    int cursorCol,
    const EditBoundary& boundary,
    BackwardEdit edit) {

    int len = static_cast<int>(editContent.size());

    if (cursorCol < 0 || cursorCol >= len) return false;

    switch (edit) {
        case BackwardEdit::CHAR:
            // X: safe if cursor > 0
            return cursorCol > 0;

        case BackwardEdit::LINE_TO_START:
            // d0, d^: only safe if edit region starts at line start
            return boundary.startsAtLineStart;

        case BackwardEdit::WORD_TO_START:
            // db: safe if boundary doesn't cut through word, OR b_landing > firstWordEnd
            if (!boundary.left_in_word) return true;
            return computeBLanding(editContent, cursorCol) > findFirstWordEnd(editContent);

        case BackwardEdit::WORD_TO_END:
            // dge: safe if boundary doesn't cut through word, OR ge_landing > firstWordEnd
            if (!boundary.left_in_word) return true;
            return computeGeLanding(editContent, cursorCol) > findFirstWordEnd(editContent);

        case BackwardEdit::BIG_WORD_TO_START:
            // dB: safe if boundary doesn't cut through WORD, OR B_landing > firstBigWordEnd
            if (!boundary.left_in_WORD) return true;
            return computeBBigLanding(editContent, cursorCol) > findFirstBigWordEnd(editContent);

        case BackwardEdit::BIG_WORD_TO_END:
            // dgE: safe if boundary doesn't cut through WORD, OR gE_landing > firstBigWordEnd
            if (!boundary.left_in_WORD) return true;
            return computeGELanding(editContent, cursorCol) > findFirstBigWordEnd(editContent);
    }

    return false; // Should not reach here
}

bool isFullLineEditSafe(const EditBoundary& boundary) {
    return boundary.startsAtLineStart && boundary.endsAtLineEnd;
}
