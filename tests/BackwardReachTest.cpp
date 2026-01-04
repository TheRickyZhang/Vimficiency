#include <gtest/gtest.h>

#include "Optimizer/BoundaryFlags.h"

#include <cstring>

using namespace std;

// =============================================================================
// Backward Reach Tests
// =============================================================================
//
// Test case: "abc def.gh i"
// Edit boundary: cols 1-8 ("bc def.g")
// - Left boundary: 'a' (col 0) is outside, 'b' (col 1) is inside
//   'a' and 'b' are in same word → left_in_word = true
// - Right boundary: 'g' (col 8) is inside, 'h' (col 9) is outside
//   'g' and 'h' are in same word → right_in_word = true
//
// For backward motions within edit content "bc def.g":
// - At col 0 ('b'): X would delete outside region → unsafe
// - At col 1 ('c'): X deletes 'b' → safe
// - db from anywhere in first word would delete into 'a' → unsafe
// - db from second word would land in first word → check if safe
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

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachTest, BoundaryFlagsCorrect) {
    // Left: 'a' (col 0) and 'b' (col 1) are in same word
    EXPECT_TRUE(boundary.left_in_word);
    EXPECT_TRUE(boundary.left_in_WORD);

    // Right: 'g' (col 8) and 'h' (col 9) are in same word
    EXPECT_TRUE(boundary.right_in_word);
    EXPECT_TRUE(boundary.right_in_WORD);
}

TEST_F(BackwardReachTest, X_AtCol0_Unsafe) {
    // X at col 0 would try to delete before edit region
    EXPECT_FALSE(isBackwardEditSafeWithContent(EDIT_CONTENT, 0, boundary, "X"));
}

TEST_F(BackwardReachTest, X_AtCol1_Safe) {
    // X at col 1 deletes col 0 within edit region
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 1, boundary, "X"));
}

TEST_F(BackwardReachTest, X_AtLastCol_Safe) {
    // X at last col deletes previous col within edit region
    int lastCol = static_cast<int>(strlen(EDIT_CONTENT)) - 1;
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, lastCol, boundary, "X"));
}

TEST_F(BackwardReachTest, db_FromFirstWord_Unsafe) {
    // "bc def.g": first word is "bc" (cols 0-1)
    // db from 'b' (col 0) or 'c' (col 1) would land at position 0
    // Since left_in_word is true, this would delete into 'a' → unsafe
    EXPECT_FALSE(isBackwardEditSafeWithContent(EDIT_CONTENT, 0, boundary, "db"));
    EXPECT_FALSE(isBackwardEditSafeWithContent(EDIT_CONTENT, 1, boundary, "db"));
}

TEST_F(BackwardReachTest, db_FromSpace_Safe) {
    // "bc def.g": space is at col 2
    // db from space lands at start of 'bc' (col 0)
    // But since left_in_word is true, db landing at 0 would delete into 'a'
    // Actually, db from space would delete the space and land at end of 'bc'
    // Wait, db deletes backward to start of previous word, so from space:
    // b_landing would be 0 ('b'), so deletion range is [0, 1]
    // Since left_in_word is true, this is unsafe
    EXPECT_FALSE(isBackwardEditSafeWithContent(EDIT_CONTENT, 2, boundary, "db"));
}

TEST_F(BackwardReachTest, db_FromSecondWord_Safe) {
    // "bc def.g": 'd' is at col 3
    // db from 'd' lands at start of 'd' (since 'd' is start of word)
    // Actually, 'd' is at start of word 'def', so db would go to start of previous word 'bc'
    // b_landing = 0, and since left_in_word is true, unsafe
    EXPECT_FALSE(isBackwardEditSafeWithContent(EDIT_CONTENT, 3, boundary, "db"));

    // 'e' is at col 4, 'f' at col 5
    // db from 'e' or 'f' lands at 'd' (col 3), which is safe
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 4, boundary, "db"));
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 5, boundary, "db"));
}

TEST_F(BackwardReachTest, db_FromPunctuation_Safe) {
    // "bc def.g": '.' is at col 6
    // db from '.' lands at start of '.' (since '.' is its own word)
    // Wait, '.' is a single-char word. db from '.' would go to start of 'def' (col 3)
    // b_landing = 3, firstWordEnd = 1 ('c')
    // 3 > 1, so safe
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 6, boundary, "db"));
}

// =============================================================================
// Backward Reach with Clean Left Boundary
// =============================================================================

class BackwardReachCleanBoundaryTest : public ::testing::Test {
protected:
    // Original: " abc def" (starts with space)
    // Edit boundary: "abc def" (cols 1-7)
    // Left: space (col 0) and 'a' (col 1) are NOT in same word
    static constexpr const char* ORIGINAL_LINE = " abc def";
    static constexpr int EDIT_START = 1;  // 'a'
    static constexpr int EDIT_END = 7;    // 'f'

    static constexpr const char* EDIT_CONTENT = "abc def";

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachCleanBoundaryTest, BoundaryNotInWord) {
    // Space and 'a' are not in same word
    EXPECT_FALSE(boundary.left_in_word);
    EXPECT_FALSE(boundary.left_in_WORD);
}

TEST_F(BackwardReachCleanBoundaryTest, X_AtCol0_StillUnsafe) {
    // X at col 0 still tries to delete outside (col -1)
    // This is a position issue, not a word boundary issue
    EXPECT_FALSE(isBackwardEditSafeWithContent(EDIT_CONTENT, 0, boundary, "X"));
}

TEST_F(BackwardReachCleanBoundaryTest, db_FromFirstWord_Safe) {
    // Since left_in_word is false, db is always safe (won't extend past boundary)
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 0, boundary, "db"));
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 1, boundary, "db"));
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 2, boundary, "db"));
}

TEST_F(BackwardReachCleanBoundaryTest, d0_Safe) {
    // d0 should be safe when left boundary is clean
    EXPECT_TRUE(isBackwardEditSafeWithContent(EDIT_CONTENT, 3, boundary, "d0"));
}

// =============================================================================
// Backward Reach with Changed Content
// =============================================================================

class BackwardReachChangedContentTest : public ::testing::Test {
protected:
    // Original: "abc def.gh i"
    // Edit boundary: "bc def.g" (cols 1-8)
    // Left boundary: 'a' and 'b' in same word → left_in_word = true
    static constexpr const char* ORIGINAL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;
    static constexpr int EDIT_END = 8;

    // After some edits, content becomes "xy z"
    static constexpr const char* NEW_CONTENT = "xy z";

    BoundaryFlags boundary;

    void SetUp() override {
        boundary = analyzeBoundaryFlags(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachChangedContentTest, X_Behavior) {
    // X at col 0 is unsafe (would delete before region)
    EXPECT_FALSE(isBackwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "X"));
    // X at cols 1-3 is safe
    EXPECT_TRUE(isBackwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "X"));
    EXPECT_TRUE(isBackwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "X"));
    EXPECT_TRUE(isBackwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "X"));
}

TEST_F(BackwardReachChangedContentTest, db_FromFirstWord_Unsafe) {
    // "xy z": first word is "xy" (cols 0-1)
    // Since left_in_word is true, db from first word is unsafe
    EXPECT_FALSE(isBackwardEditSafeWithContent(NEW_CONTENT, 0, boundary, "db"));
    EXPECT_FALSE(isBackwardEditSafeWithContent(NEW_CONTENT, 1, boundary, "db"));
}

TEST_F(BackwardReachChangedContentTest, db_FromSpace_Unsafe) {
    // "xy z": space at col 2
    // db from space lands at start of 'xy' (col 0)
    // Since left_in_word is true, unsafe
    EXPECT_FALSE(isBackwardEditSafeWithContent(NEW_CONTENT, 2, boundary, "db"));
}

TEST_F(BackwardReachChangedContentTest, db_FromSecondWord_Safe) {
    // "xy z": 'z' at col 3
    // db from 'z' lands at start of 'z' (since it's a single char word at start)
    // Actually, 'z' is at col 3, and it's a single-char word
    // db from 'z' would go to start of previous word 'xy' (col 0)
    // b_landing = 0, firstWordEnd = 1
    // 0 > 1 is false → unsafe
    EXPECT_FALSE(isBackwardEditSafeWithContent(NEW_CONTENT, 3, boundary, "db"));
}
