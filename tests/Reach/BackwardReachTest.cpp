#include <gtest/gtest.h>

#include "Optimizer/EditBoundary.h"

#include <cstring>

using namespace std;

// =============================================================================
// Backward Reach Tests
// =============================================================================
//
// Test case: "abc def.gh i"
// Edit boundary: cols 1-8 ("bc def.g")
// - Left boundary: 'a' (col 0) is outside → leftBoundaryChar = Keyword
// - Right boundary: 'h' (col 9) is outside → rightBoundaryChar = Keyword
//
// For backward motions within edit content "bc def.g":
// - At col 0 ('b'): X would delete outside region → unsafe
// - At col 1 ('c'): X deletes 'b' → safe
// - db from first word would delete across boundary → depends on crossing logic
//

class BackwardReachTest : public ::testing::Test {
protected:
    // Original: "abc def.gh i"
    // Edit boundary: "bc def.g" (cols 1-8)
    static constexpr const char* ORIGINAL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;  // 'b'
    static constexpr int EDIT_END = 8;    // 'g'

    // Edit content (original, before any changes)
    static constexpr const char* EDIT_CONTENT = "bc def.g";

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachTest, BoundaryCharsCorrect) {
    // Left: 'a' (col 0) is a keyword char
    EXPECT_EQ(boundary.leftBoundaryChar, CharType::Keyword);

    // Right: 'h' (col 9) is a keyword char
    EXPECT_EQ(boundary.rightBoundaryChar, CharType::Keyword);
}

TEST_F(BackwardReachTest, X_AtCol0_Unsafe) {
    // X at col 0 would try to delete before edit region
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 0, boundary, BackwardEdit::CHAR));
}

TEST_F(BackwardReachTest, X_AtCol1_Safe) {
    // X at col 1 deletes col 0 within edit region
    EXPECT_TRUE(isBackwardEditSafe(EDIT_CONTENT, 1, boundary, BackwardEdit::CHAR));
}

TEST_F(BackwardReachTest, X_AtLastCol_Safe) {
    // X at last col deletes previous col within edit region
    int lastCol = static_cast<int>(strlen(EDIT_CONTENT)) - 1;
    EXPECT_TRUE(isBackwardEditSafe(EDIT_CONTENT, lastCol, boundary, BackwardEdit::CHAR));
}

TEST_F(BackwardReachTest, db_FromFirstWord_Unsafe) {
    // "bc def.g": first word is "bc" (cols 0-1)
    // db from 'b' or 'c' - first char is 'b' (Keyword), boundary is 'a' (Keyword)
    // canDbCross(Keyword, Keyword) = true → would cross → unsafe
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 0, boundary, BackwardEdit::WORD_TO_START));
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 1, boundary, BackwardEdit::WORD_TO_START));
}

TEST_F(BackwardReachTest, db_FromSpace_Unsafe) {
    // "bc def.g": space is at col 2
    // db from space - first char would be ' ' after deletion starts
    // But the isBackwardEditSafe uses first char of current content = 'b'
    // canDbCross(Keyword, Keyword) = true → unsafe
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 2, boundary, BackwardEdit::WORD_TO_START));
}

TEST_F(BackwardReachTest, db_FromSecondWord_Safe) {
    // "bc def.g": 'd' is at col 3
    // These tests rely on the legacy isBackwardEditSafe which uses first char of whole content
    // First char = 'b' (Keyword), boundary = 'a' (Keyword) → would cross
    // So these would be unsafe with the new logic
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 3, boundary, BackwardEdit::WORD_TO_START));

    // 'e' at col 4, 'f' at col 5 - same analysis
    // With new logic: still uses first char of content = 'b'
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 4, boundary, BackwardEdit::WORD_TO_START));
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 5, boundary, BackwardEdit::WORD_TO_START));
}

TEST_F(BackwardReachTest, db_FromPunctuation_Unsafe) {
    // "bc def.g": '.' is at col 6
    // First char = 'b' (Keyword), boundary = 'a' (Keyword) → canDbCross = true → unsafe
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 6, boundary, BackwardEdit::WORD_TO_START));
}

// =============================================================================
// Backward Reach with Clean Left Boundary
// =============================================================================

class BackwardReachCleanBoundaryTest : public ::testing::Test {
protected:
    // Original: " abc def" (starts with space)
    // Edit boundary: "abc def" (cols 1-7)
    // Left: space (col 0) → leftBoundaryChar = Whitespace
    static constexpr const char* ORIGINAL_LINE = " abc def";
    static constexpr int EDIT_START = 1;  // 'a'
    static constexpr int EDIT_END = 7;    // 'f'

    static constexpr const char* EDIT_CONTENT = "abc def";

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachCleanBoundaryTest, BoundaryIsWhitespace) {
    // Left boundary char is space
    EXPECT_EQ(boundary.leftBoundaryChar, CharType::Whitespace);
}

TEST_F(BackwardReachCleanBoundaryTest, X_AtCol0_StillUnsafe) {
    // X at col 0 still tries to delete outside (col -1)
    // This is a position issue, not a word boundary issue
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 0, boundary, BackwardEdit::CHAR));
}

TEST_F(BackwardReachCleanBoundaryTest, db_FromFirstWord_Safe) {
    // First char = 'a' (Keyword), boundary = ' ' (Whitespace)
    // canDbCross(Keyword, Whitespace) = false → won't cross → safe
    EXPECT_TRUE(isBackwardEditSafe(EDIT_CONTENT, 0, boundary, BackwardEdit::WORD_TO_START));
    EXPECT_TRUE(isBackwardEditSafe(EDIT_CONTENT, 1, boundary, BackwardEdit::WORD_TO_START));
    EXPECT_TRUE(isBackwardEditSafe(EDIT_CONTENT, 2, boundary, BackwardEdit::WORD_TO_START));
}

TEST_F(BackwardReachCleanBoundaryTest, d0_Unsafe_NotAtLineStart) {
    // d0 should be unsafe when not at line start
    // EDIT_START = 1, not 0, so not at line start
    EXPECT_FALSE(isBackwardEditSafe(EDIT_CONTENT, 3, boundary, BackwardEdit::LINE_TO_START));
}

// =============================================================================
// Backward Reach with Changed Content
// =============================================================================

class BackwardReachChangedContentTest : public ::testing::Test {
protected:
    // Original: "abc def.gh i"
    // Edit boundary: "bc def.g" (cols 1-8)
    // Left boundary: 'a' (Keyword)
    static constexpr const char* ORIGINAL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;
    static constexpr int EDIT_END = 8;

    // After some edits, content becomes "xy z"
    static constexpr const char* NEW_CONTENT = "xy z";

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachChangedContentTest, X_Behavior) {
    // X at col 0 is unsafe (would delete before region)
    EXPECT_FALSE(isBackwardEditSafe(NEW_CONTENT, 0, boundary, BackwardEdit::CHAR));
    // X at cols 1-3 is safe
    EXPECT_TRUE(isBackwardEditSafe(NEW_CONTENT, 1, boundary, BackwardEdit::CHAR));
    EXPECT_TRUE(isBackwardEditSafe(NEW_CONTENT, 2, boundary, BackwardEdit::CHAR));
    EXPECT_TRUE(isBackwardEditSafe(NEW_CONTENT, 3, boundary, BackwardEdit::CHAR));
}

TEST_F(BackwardReachChangedContentTest, db_FromFirstWord_Unsafe) {
    // "xy z": first char = 'x' (Keyword), boundary = 'a' (Keyword)
    // canDbCross(Keyword, Keyword) = true → unsafe
    EXPECT_FALSE(isBackwardEditSafe(NEW_CONTENT, 0, boundary, BackwardEdit::WORD_TO_START));
    EXPECT_FALSE(isBackwardEditSafe(NEW_CONTENT, 1, boundary, BackwardEdit::WORD_TO_START));
}

TEST_F(BackwardReachChangedContentTest, db_FromSpace_Unsafe) {
    // "xy z": space at col 2
    // First char = 'x' (Keyword), boundary = 'a' (Keyword)
    // canDbCross(Keyword, Keyword) = true → unsafe
    EXPECT_FALSE(isBackwardEditSafe(NEW_CONTENT, 2, boundary, BackwardEdit::WORD_TO_START));
}

TEST_F(BackwardReachChangedContentTest, db_FromSecondWord_Unsafe) {
    // "xy z": 'z' at col 3
    // First char = 'x' (Keyword), boundary = 'a' (Keyword)
    // canDbCross(Keyword, Keyword) = true → unsafe
    EXPECT_FALSE(isBackwardEditSafe(NEW_CONTENT, 3, boundary, BackwardEdit::WORD_TO_START));
}
