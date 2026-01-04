#include <gtest/gtest.h>

#include "Editor/Edit.h"
#include "Editor/Motion.h"
#include "Editor/Range.h"
#include "VimCore/VimEditUtils.h"
#include "Optimizer/BoundaryFlags.h"
#include "Optimizer/ReachLevel.h"

using namespace std;

// =============================================================================
// Forward Reach Test Suite
// Tests that forward edit operations respect edit boundaries
// =============================================================================
//
// Test case: "abc def.gh i"
// Edit boundary: cols 1-8 ("bc def.g")
// The edit should NOT modify 'a' (col 0) or 'h' (col 9)
//
// Vim-tested expected results:
// - At col 8 (g): only x works
// - At col 7 (.): x, dw work
// - At col 6 (f): x, dw, de work
// - At col 3 (space): x, dw, de, dW work
// - At col 1 (b): x, dw, de, dW, dE work
// - D never works (would delete to EOL)
//
// =============================================================================

class ForwardReachTest : public ::testing::Test {
protected:
    static constexpr const char* FULL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;  // 'b'
    static constexpr int EDIT_END = 8;    // 'g' (inclusive)

    // Apply a single-char edit (x, D) and return the resulting line
    string applySimpleEdit(const string& line, int cursorCol, const string& editCmd) {
        Lines lines = {line};
        Position pos(0, cursorCol);
        Mode mode = Mode::Normal;
        NavContext nav(0, 40, 40, 20);

        Edit::applyEdit(lines, pos, mode, nav, ParsedEdit(editCmd));
        return lines[0];
    }

    // Apply operator+motion edit (dw, de, dW, dE) and return the resulting line
    string applyOperatorMotion(const string& line, int cursorCol, const string& motion) {
        Lines lines = {line};
        Position startPos(0, cursorCol);
        Mode mode = Mode::Normal;
        NavContext nav(0, 40, 40, 20);

        // Simulate the motion to get end position
        MotionResult result = simulateMotions(startPos, mode, nav, motion, lines);
        Position endPos = result.pos;

        // Determine inclusivity based on motion
        // e, E are inclusive (delete up to and including end char)
        // w, W, b, B are exclusive (delete up to but not including end char)
        bool inclusive = (motion == "e" || motion == "E" ||
                          motion == "ge" || motion == "gE");

        // For exclusive motions like w/W, we want to delete up to (not including) the landing spot
        // So we adjust: delete [start, end-1] inclusive, or [start, end) exclusive
        Range range = rangeFromMotion(startPos, endPos, inclusive);

        // For w/W motions: range is [start, end) exclusive
        // But our deleteRange expects inclusive end, so adjust
        if (!inclusive && range.end.col > 0) {
            range.end.col--;
            range.inclusive = true;
        }

        VimEditUtils::deleteRange(lines, range, startPos);
        return lines[0];
    }

    // Check if an edit stays within the boundary
    // (i.e., doesn't modify chars outside [EDIT_START, EDIT_END])
    bool isEditSafe(const string& original, const string& result, int editStart, int editEnd) {
        // Characters before editStart must be unchanged
        for (int i = 0; i < editStart && i < (int)original.size() && i < (int)result.size(); i++) {
            if (original[i] != result[i]) return false;
        }

        // Characters after editEnd must be unchanged (accounting for deletions)
        // Find what remains after the edit boundary in result
        int origAfterEdit = editEnd + 1;
        string origSuffix = original.substr(origAfterEdit);

        // The result should end with this suffix
        if (result.size() < origSuffix.size()) return false;
        string resultSuffix = result.substr(result.size() - origSuffix.size());

        return origSuffix == resultSuffix;
    }
};

// =============================================================================
// At col 8 (g): only x works
// =============================================================================

TEST_F(ForwardReachTest, AtG_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 8, "x");
    EXPECT_EQ(result, "abc def.h i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtG_De_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 8, "e");
    // de from g would delete 'gh' (to end of word), leaving "abc def. i"
    // This deletes 'h' which is outside the edit boundary
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

// =============================================================================
// At col 7 (.): x, dw work
// =============================================================================

TEST_F(ForwardReachTest, AtDot_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 7, "x");
    EXPECT_EQ(result, "abc defgh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtDot_Dw_Works) {
    string result = applyOperatorMotion(FULL_LINE, 7, "w");
    // dw from . goes to next word 'gh', deleting just '.'
    EXPECT_EQ(result, "abc defgh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtDot_De_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 7, "e");
    // de from . (end of word '.') goes to end of next word 'gh' (at 'h')
    // This deletes '.gh', leaving "abc def i" - 'h' was outside boundary
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

// =============================================================================
// At col 6 (f): x, dw, de work
// =============================================================================

TEST_F(ForwardReachTest, AtF_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 6, "x");
    EXPECT_EQ(result, "abc de.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtF_Dw_Works) {
    string result = applyOperatorMotion(FULL_LINE, 6, "w");
    // dw from f goes to '.', deleting 'f'
    EXPECT_EQ(result, "abc de.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtF_De_Works) {
    string result = applyOperatorMotion(FULL_LINE, 6, "e");
    // de from f (end of 'def') goes to end of next word '.' (at '.')
    // Deletes 'f.' leaving "abc degh i"
    EXPECT_EQ(result, "abc degh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtF_DW_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 6, "W");
    // dW from f goes to next WORD 'i', deleting 'f.gh '
    // This deletes 'h' which is outside boundary
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

// =============================================================================
// At col 3 (space): x, dw, de, dW work
// =============================================================================

TEST_F(ForwardReachTest, AtSpace_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 3, "x");
    EXPECT_EQ(result, "abcdef.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtSpace_Dw_Works) {
    string result = applyOperatorMotion(FULL_LINE, 3, "w");
    // dw from space goes to 'd', deleting just the space
    EXPECT_EQ(result, "abcdef.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtSpace_De_Works) {
    string result = applyOperatorMotion(FULL_LINE, 3, "e");
    // de from space goes to end of 'def' (at 'f'), deleting ' def'
    EXPECT_EQ(result, "abc.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtSpace_DW_Works) {
    string result = applyOperatorMotion(FULL_LINE, 3, "W");
    // dW from space goes to 'd' (start of WORD 'def.gh'), deleting just space
    EXPECT_EQ(result, "abcdef.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtSpace_DE_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 3, "E");
    // dE from space goes to end of WORD 'def.gh' (at 'h'), deleting ' def.gh'
    // This deletes 'h' which is outside boundary
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

// =============================================================================
// At col 1 (b): x, dw, de, dW, dE work
// =============================================================================

TEST_F(ForwardReachTest, AtB_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 1, "x");
    EXPECT_EQ(result, "ac def.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtB_Dw_Works) {
    string result = applyOperatorMotion(FULL_LINE, 1, "w");
    // dw from b goes to 'd' (start of next word), deleting 'bc '
    EXPECT_EQ(result, "adef.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtB_De_Works) {
    string result = applyOperatorMotion(FULL_LINE, 1, "e");
    // de from b goes to end of 'abc' (at 'c'), deleting 'bc'
    EXPECT_EQ(result, "a def.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtB_DW_Works) {
    string result = applyOperatorMotion(FULL_LINE, 1, "w");
    // dW from b goes to 'd' (start of next WORD), deleting 'bc '
    EXPECT_EQ(result, "adef.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtB_DE_Works) {
    string result = applyOperatorMotion(FULL_LINE, 1, "E");
    // dE from b goes to end of WORD 'abc' (at 'c'), deleting 'bc'
    EXPECT_EQ(result, "a def.gh i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtB_D_Fails) {
    string result = applySimpleEdit(FULL_LINE, 1, "D");
    // D from b deletes to EOL: 'bc def.gh i'
    // This deletes everything including 'h' and 'i'
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

// =============================================================================
// Tests for isForwardEditSafe() prediction function
// These verify that the prediction matches actual edit behavior
// =============================================================================

class ForwardReachPredictionTest : public ::testing::Test {
protected:
    static constexpr const char* FULL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;  // 'b'
    static constexpr int EDIT_END = 8;    // 'g' (inclusive)

    ForwardReachInfo info;

    void SetUp() override {
        info = analyzeForwardBoundary(FULL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(ForwardReachPredictionTest, AnalyzeBoundary_Correctly) {
    // 'g' (col 8) and 'h' (col 9) are in same word 'gh'
    EXPECT_TRUE(info.boundary_in_word);
    // 'g' and 'h' are also in same WORD 'def.gh'
    EXPECT_TRUE(info.boundary_in_WORD);

    // Last word start is at 'g' (col 8) - the word is 'gh'
    EXPECT_EQ(info.lastWordStart, 8);

    // Last WORD start is at 'd' (col 4) - the WORD is 'def.gh'
    EXPECT_EQ(info.lastBigWordStart, 4);

    // Last safe word end: the word before 'gh' is '.' at col 7
    EXPECT_EQ(info.lastSafeWordEnd, 7);

    // Last safe WORD end: the WORD before 'def.gh' is 'abc' ending at col 2
    EXPECT_EQ(info.lastSafeBigWordEnd, 2);
}

TEST_F(ForwardReachPredictionTest, AtG_Predictions) {
    // At col 8 (g): only x should be safe
    EXPECT_TRUE(isForwardEditSafe(info, 8, "x"));
    EXPECT_FALSE(isForwardEditSafe(info, 8, "de"));
    EXPECT_FALSE(isForwardEditSafe(info, 8, "dw"));
    EXPECT_FALSE(isForwardEditSafe(info, 8, "dW"));
    EXPECT_FALSE(isForwardEditSafe(info, 8, "dE"));
    EXPECT_FALSE(isForwardEditSafe(info, 8, "D"));
}

TEST_F(ForwardReachPredictionTest, AtDot_Predictions) {
    // At col 7 (.): x, dw should be safe; de should fail
    EXPECT_TRUE(isForwardEditSafe(info, 7, "x"));
    EXPECT_TRUE(isForwardEditSafe(info, 7, "dw"));
    EXPECT_FALSE(isForwardEditSafe(info, 7, "de"));
    EXPECT_FALSE(isForwardEditSafe(info, 7, "dW"));
    EXPECT_FALSE(isForwardEditSafe(info, 7, "dE"));
}

TEST_F(ForwardReachPredictionTest, AtF_Predictions) {
    // At col 6 (f): x, dw, de should be safe; dW, dE should fail
    EXPECT_TRUE(isForwardEditSafe(info, 6, "x"));
    EXPECT_TRUE(isForwardEditSafe(info, 6, "dw"));
    EXPECT_TRUE(isForwardEditSafe(info, 6, "de"));
    EXPECT_FALSE(isForwardEditSafe(info, 6, "dW"));
    EXPECT_FALSE(isForwardEditSafe(info, 6, "dE"));
}

TEST_F(ForwardReachPredictionTest, AtSpace_Predictions) {
    // At col 3 (space): x, dw, de, dW should be safe; dE should fail
    EXPECT_TRUE(isForwardEditSafe(info, 3, "x"));
    EXPECT_TRUE(isForwardEditSafe(info, 3, "dw"));
    EXPECT_TRUE(isForwardEditSafe(info, 3, "de"));
    EXPECT_TRUE(isForwardEditSafe(info, 3, "dW"));
    EXPECT_FALSE(isForwardEditSafe(info, 3, "dE"));
}

TEST_F(ForwardReachPredictionTest, AtB_Predictions) {
    // At col 1 (b): x, dw, de, dW, dE should be safe; D should fail
    EXPECT_TRUE(isForwardEditSafe(info, 1, "x"));
    EXPECT_TRUE(isForwardEditSafe(info, 1, "dw"));
    EXPECT_TRUE(isForwardEditSafe(info, 1, "de"));
    EXPECT_TRUE(isForwardEditSafe(info, 1, "dW"));
    EXPECT_TRUE(isForwardEditSafe(info, 1, "dE"));
    EXPECT_FALSE(isForwardEditSafe(info, 1, "D"));
}

// =============================================================================
// Tests for isForwardEditSafeWithContent (new API for changed content)
// =============================================================================
//
// This tests the key insight: analyze boundary ONCE, then use with ANY content.
//
// Original: "abc def.gh i", edit bounds [1,8] ("bc def.g")
// Boundary analysis: g and h are same word → right_in_word = true
//
// After edit, content is "x yz" (full text: "ax yzh i")
// The last word "yz" connects to "h" outside → any edit hitting "yz" is unsafe
//
// Expected (from user's vim testing):
// - At z: only x works
// - At y: only x works (dw fails!)
// - At space: x, dw work (de fails!)
// - At x: x, dw work (de fails!)
//

class ForwardReachChangedContentTest : public ::testing::Test {
protected:
    // Original text for boundary analysis
    static constexpr const char* ORIGINAL_LINE = "abc def.gh i";
    static constexpr int EDIT_START_IN_ORIGINAL = 1;  // 'b'
    static constexpr int EDIT_END_IN_ORIGINAL = 8;  // 'g'

    // New content after edit
    static constexpr const char* NEW_CONTENT = "x yz";

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START_IN_ORIGINAL, EDIT_END_IN_ORIGINAL);
    }
};

TEST_F(ForwardReachChangedContentTest, BoundaryFlagsCorrect) {
    // 'g' (col 8) and 'h' (col 9) are in same word 'gh'
    EXPECT_TRUE(boundary.right_in_word);
    // They're also in same WORD 'def.gh'
    EXPECT_TRUE(boundary.right_in_WORD);
}

TEST_F(ForwardReachChangedContentTest, AtZ_OnlyXWorks) {
    // "x yz" positions: x=0, space=1, y=2, z=3
    // At z (col 3): only x should work
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "x"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "dw"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "de"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "dW"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "dE"));
}

TEST_F(ForwardReachChangedContentTest, AtY_OnlyXWorks) {
    // At y (col 2): only x should work
    // dw from y goes to next word which is outside (since yz connects to h)
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "x"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "dw"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "de"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "dW"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "dE"));
}

TEST_F(ForwardReachChangedContentTest, AtSpace_XAndDwWork) {
    // At space (col 1): x, dw should work; de, dW, dE should fail
    // dw from space goes to 'y' which is still in content
    // de from space goes to end of 'yz' but yz extends to h → unsafe
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "x"));
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "dw"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "de"));
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "dW"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "dE"));
}

TEST_F(ForwardReachChangedContentTest, AtX_XAndDwWork) {
    // At x (col 0): x, dw should work; de should fail
    // dw from x goes to 'y' (start of next word)
    // de from x (end of word 'x') goes to end of next word 'yz', but yz extends to h → unsafe
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "x"));
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "dw"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "de"));
    EXPECT_TRUE(isForwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "dW"));
    EXPECT_FALSE(isForwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "dE"));
}

// =============================================================================
// Edge Cases: Boundary does NOT cut through word
// =============================================================================
//
// When boundary ends cleanly at word boundary (e.g., "abc " followed by "def"),
// all word-based motions should be safe since they won't cross into the next word.
//

class ForwardReachCleanBoundaryTest : public ::testing::Test {
protected:
    // Original: "abc def ghi"
    // Edit boundary at space: "abc " (cols 0-3), next char is 'd'
    // Space followed by 'd' are NOT in same word (space is blank)
    static constexpr const char* ORIGINAL_LINE = "abc def ghi";
    static constexpr int EDIT_START_IN_ORIGINAL = 0;  // 'a'
    static constexpr int EDIT_END_IN_ORIGINAL = 3;  // space

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START_IN_ORIGINAL, EDIT_END_IN_ORIGINAL);
    }
};

TEST_F(ForwardReachCleanBoundaryTest, BoundaryNotInWord) {
    // Space (col 3) followed by 'd' (col 4) - not in same word
    EXPECT_FALSE(boundary.right_in_word);
    EXPECT_FALSE(boundary.right_in_WORD);
}

TEST_F(ForwardReachCleanBoundaryTest, AllMotionsSafe) {
    // With clean boundary, all word-based motions should be safe
    string content = "abc ";  // The edit content

    for (int col = 0; col <= 3; col++) {
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "x"))
            << "x should be safe at col " << col;
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "dw"))
            << "dw should be safe at col " << col;
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "de"))
            << "de should be safe at col " << col;
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "dW"))
            << "dW should be safe at col " << col;
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "dE"))
            << "dE should be safe at col " << col;
    }
}

TEST_F(ForwardReachCleanBoundaryTest, DStillUnsafe) {
    // D deletes to end of line, which goes beyond our edit region
    // But wait - if boundary is at space and next is 'd', the edit ends at EOL
    // In this case our boundary analysis says right_in_word=false, right_in_WORD=false
    // So D should actually be safe!
    string content = "abc ";
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 0, boundary, "D"));
}

// =============================================================================
// Edge Cases: Single character content
// =============================================================================

class ForwardReachSingleCharTest : public ::testing::Test {
protected:
    // Original: "abcd"
    // Edit boundary: just 'b' (col 1), next char is 'c'
    // 'b' and 'c' are in same word, 'a' and 'b' are also in same word
    static constexpr const char* ORIGINAL_LINE = "abcd";
    static constexpr int EDIT_START_IN_ORIGINAL = 1;  // 'b'
    static constexpr int EDIT_END_IN_ORIGINAL = 1;  // 'b'

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START_IN_ORIGINAL, EDIT_END_IN_ORIGINAL);
    }
};

TEST_F(ForwardReachSingleCharTest, BoundaryInWord) {
    EXPECT_TRUE(boundary.right_in_word);
    EXPECT_TRUE(boundary.right_in_WORD);
}

TEST_F(ForwardReachSingleCharTest, OnlyXSafe) {
    string content = "b";  // Single char

    // x at col 0: safe (deletes 'b')
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 0, boundary, "x"));

    // dw: cursor=0, lastWordStart=0 → 0 < 0 is false → unsafe
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 0, boundary, "dw"));

    // de: e_landing=0 (at end of word 'b'), lastWordStart=0 → 0 < 0 is false → unsafe
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 0, boundary, "de"));
}

// =============================================================================
// Edge Cases: Content ending with whitespace
// =============================================================================

class ForwardReachTrailingSpaceTest : public ::testing::Test {
protected:
    // Original: "abc  def"
    // Edit boundary: "bc " (cols 1-3), next char is ' ' (col 4)
    // Space followed by space - not in same word
    // 'a' (col 0) and 'b' (col 1) are in same word
    static constexpr const char* ORIGINAL_LINE = "abc  def";
    static constexpr int EDIT_START_IN_ORIGINAL = 1;  // 'b'
    static constexpr int EDIT_END_IN_ORIGINAL = 3;  // first space

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START_IN_ORIGINAL, EDIT_END_IN_ORIGINAL);
    }
};

TEST_F(ForwardReachTrailingSpaceTest, BoundaryNotInWord) {
    // Space followed by space - neither is a word char
    EXPECT_FALSE(boundary.right_in_word);
    EXPECT_FALSE(boundary.right_in_WORD);
}

TEST_F(ForwardReachTrailingSpaceTest, AllMotionsSafe) {
    string content = "bc ";

    // All positions should allow all motions
    for (int col = 0; col <= 2; col++) {
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "dw"))
            << "dw should be safe at col " << col;
        EXPECT_TRUE(isForwardEditSafeWithContent(content, col, boundary, "de"))
            << "de should be safe at col " << col;
    }
}

// =============================================================================
// Edge Cases: Punctuation boundaries (word vs WORD)
// =============================================================================
//
// "abc." where boundary is at '.', next char is 'd'
// '.' and 'd' are NOT in same word (different word types)
// BUT '.' and 'd' ARE in same WORD (both non-blank)
//

class ForwardReachPunctuationBoundaryTest : public ::testing::Test {
protected:
    // Original: "abc.def"
    // Edit boundary at '.': cols 0-3, next char is 'd'
    static constexpr const char* ORIGINAL_LINE = "abc.def";
    static constexpr int EDIT_START_IN_ORIGINAL = 0;  // 'a'
    static constexpr int EDIT_END_IN_ORIGINAL = 3;  // '.'

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START_IN_ORIGINAL, EDIT_END_IN_ORIGINAL);
    }
};

TEST_F(ForwardReachPunctuationBoundaryTest, BoundaryInWORDNotWord) {
    // '.' and 'd' are different word types → not in same word
    EXPECT_FALSE(boundary.right_in_word);
    // '.' and 'd' are both non-blank → in same WORD
    EXPECT_TRUE(boundary.right_in_WORD);
}

TEST_F(ForwardReachPunctuationBoundaryTest, WordMotionsSafeWORDMotionsUnsafe) {
    string content = "abc.";

    // dw, de should be safe (word boundary is clean)
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 0, boundary, "dw"));
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 0, boundary, "de"));
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 3, boundary, "dw"));
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 3, boundary, "de"));

    // dW, dE should be unsafe at positions where they'd hit the last WORD
    // lastBigWordStart for "abc." is 0 (whole thing is one WORD)
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 0, boundary, "dW"));
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 0, boundary, "dE"));
}

// =============================================================================
// Edge Cases: Multiple words, verifying dw vs de asymmetry
// =============================================================================
//
// Key test: at whitespace, de is more aggressive than dw
// "a b c" where boundary cuts through 'c' (i.e., "a b c" + "d" outside)
//

class ForwardReachDwDeAsymmetryTest : public ::testing::Test {
protected:
    // Original: "a b cd"
    // Edit boundary at 'c': cols 0-4, next char is 'd'
    // 'c' and 'd' are in same word
    static constexpr const char* ORIGINAL_LINE = "a b cd";
    static constexpr int EDIT_START_IN_ORIGINAL = 0;  // 'a'
    static constexpr int EDIT_END_IN_ORIGINAL = 4;  // 'c'

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START_IN_ORIGINAL, EDIT_END_IN_ORIGINAL);
    }
};

TEST_F(ForwardReachDwDeAsymmetryTest, BoundaryInWord) {
    EXPECT_TRUE(boundary.right_in_word);
    EXPECT_TRUE(boundary.right_in_WORD);
}

TEST_F(ForwardReachDwDeAsymmetryTest, AtFirstSpace_DwSafeDeUnsafe) {
    // Content: "a b c"
    // At space (col 1):
    //   dw deletes " " (just space, lands at 'b') → safe
    //   de deletes " b" (goes to end of 'b') → e_landing = 2, lastWordStart = 4 → safe!
    // Wait, that's not the asymmetry case...

    string content = "a b c";
    int lastWordStartPos = 4;  // 'c'

    // At col 1 (first space):
    // dw: 1 < 4 → safe
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 1, boundary, "dw"));
    // de: e from space at col 1 goes to end of 'b' which is col 2
    //     e_landing = 2, 2 < 4 → safe
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 1, boundary, "de"));

    // At col 3 (second space):
    // dw: 3 < 4 → safe
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 3, boundary, "dw"));
    // de: e from space at col 3 goes to end of 'c' which is col 4
    //     e_landing = 4, 4 < 4 is false → unsafe!
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 3, boundary, "de"));
}

TEST_F(ForwardReachDwDeAsymmetryTest, AtB_DwSafeDeUnsafe) {
    string content = "a b c";

    // At 'b' (col 2):
    // dw: 2 < 4 → safe
    EXPECT_TRUE(isForwardEditSafeWithContent(content, 2, boundary, "dw"));
    // de: e from 'b' (single char word) goes to end of current word 'b' = col 2
    //     But wait, 'b' is at col 2 which is end of word 'b'
    //     So e_landing = findNextWordEnd = col 4 ('c')
    //     4 < 4 is false → unsafe
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 2, boundary, "de"));
}

// =============================================================================
// Edge Cases: Empty edit content (degenerate case)
// =============================================================================

TEST(ForwardReachEdgeCases, EmptyContent) {
    BoundaryFlags boundary{true, true, false, false};
    string content = "";

    // x at invalid position should be unsafe
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 0, boundary, "x"));
    EXPECT_FALSE(isForwardEditSafeWithContent(content, -1, boundary, "x"));
}

// =============================================================================
// Edge Cases: Cursor out of bounds
// =============================================================================

TEST(ForwardReachEdgeCases, CursorOutOfBounds) {
    BoundaryFlags boundary{true, true, false, false};
    string content = "abc";

    // Cursor past end of content
    EXPECT_FALSE(isForwardEditSafeWithContent(content, 5, boundary, "x"));
    // Cursor at negative position
    EXPECT_FALSE(isForwardEditSafeWithContent(content, -1, boundary, "x"));
}
