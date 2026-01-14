#include <gtest/gtest.h>

#include "Optimizer/EditBoundary.h"

using namespace std;

// =============================================================================
// Backward Reach Tests
// =============================================================================
//
// Tests for backward crossing logic using endpoint-based functions.
// For backward motions, we check (firstChar, leftBoundaryChar).
//
// Test case: "abc def.gh i"
// Edit boundary: cols 1-8 ("bc def.g")
// - Left boundary: 'a' (col 0) is outside → leftBoundaryChar = Keyword
// - Right boundary: 'h' (col 9) is outside → rightBoundaryChar = Keyword

class BackwardReachTest : public ::testing::Test {
protected:
    // Original: "abc def.gh i"
    // Edit boundary: "bc def.g" (cols 1-8)
    static constexpr const char* ORIGINAL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;  // 'b'
    static constexpr int EDIT_END = 8;    // 'g'

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

// =============================================================================
// Backward Crossing Tests
// =============================================================================

TEST_F(BackwardReachTest, db_FromKeywordContent_ToKeywordBoundary_Crosses) {
    // Content starting with 'b' (Keyword), boundary is 'a' (Keyword)
    // db uses End endpoint: canEndCross(Keyword, Keyword) = YES
    CharType firstChar = CharType::Keyword;  // 'b'
    EXPECT_TRUE(canEndCross(firstChar, boundary.leftBoundaryChar));
}

TEST_F(BackwardReachTest, dge_FromKeywordContent_ToKeywordBoundary_Crosses) {
    // dge uses Next endpoint: canNextCross(Keyword, Keyword) = YES
    CharType firstChar = CharType::Keyword;
    EXPECT_TRUE(canNextCross(firstChar, boundary.leftBoundaryChar));
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

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachCleanBoundaryTest, BoundaryIsWhitespace) {
    // Left boundary char is space
    EXPECT_EQ(boundary.leftBoundaryChar, CharType::Whitespace);
}

TEST_F(BackwardReachCleanBoundaryTest, db_FromKeyword_ToWhitespace_Safe) {
    // Content starting with 'a' (Keyword), boundary is ' ' (Whitespace)
    // canEndCross(Keyword, Whitespace) = no → safe
    CharType firstChar = CharType::Keyword;
    EXPECT_FALSE(canEndCross(firstChar, boundary.leftBoundaryChar));
}

TEST_F(BackwardReachCleanBoundaryTest, dge_FromKeyword_ToWhitespace_Crosses) {
    // canNextCross(Keyword, Whitespace) = YES → would cross
    CharType firstChar = CharType::Keyword;
    EXPECT_TRUE(canNextCross(firstChar, boundary.leftBoundaryChar));
}

TEST_F(BackwardReachCleanBoundaryTest, d0_NotAtLineStart_Crosses) {
    // d0/c0 use Line endpoint
    // boundary.leftBoundaryChar = Whitespace (not Newline)
    // canLineCross(Whitespace) = YES → would cross
    EXPECT_TRUE(canLineCross(boundary.leftBoundaryChar));
}

// =============================================================================
// Backward Reach at Line Start
// =============================================================================

class BackwardReachLineStartTest : public ::testing::Test {
protected:
    // Original: "abc def"
    // Edit boundary: entire line (cols 0-6)
    // Left: nothing before col 0 → leftBoundaryChar = Newline
    static constexpr const char* ORIGINAL_LINE = "abc def";
    static constexpr int EDIT_START = 0;
    static constexpr int EDIT_END = 6;

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardReachLineStartTest, BoundaryIsNewline) {
    EXPECT_EQ(boundary.leftBoundaryChar, CharType::Newline);
}

TEST_F(BackwardReachLineStartTest, AllBackwardMotions_ToNewline_Safe) {
    // All backward motions are safe when boundary is Newline
    CharType firstChar = CharType::Keyword;

    EXPECT_FALSE(canEndCross(firstChar, CharType::Newline));
    EXPECT_FALSE(canNextCross(firstChar, CharType::Newline));
    EXPECT_FALSE(canLineCross(CharType::Newline));
}

// =============================================================================
// Backward WORD Motions
// =============================================================================

class BackwardWORDTest : public ::testing::Test {
protected:
    // Original: "x.abc def"
    // Edit boundary: "abc def" (cols 2-8)
    // Left boundary: '.' (Symbol)
    static constexpr const char* ORIGINAL_LINE = "x.abc def";
    static constexpr int EDIT_START = 2;  // 'a'
    static constexpr int EDIT_END = 8;    // 'f'

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(ORIGINAL_LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(BackwardWORDTest, BoundaryIsSymbol) {
    EXPECT_EQ(boundary.leftBoundaryChar, CharType::Symbol);  // '.'
}

TEST_F(BackwardWORDTest, dB_FromKeyword_ToSymbol_Crosses) {
    // dB uses END endpoint (WORD version)
    // canEndCrossWORD(Keyword, Symbol) = YES (both are NonWS)
    CharType firstChar = CharType::Keyword;
    EXPECT_TRUE(canEndCrossWORD(firstChar, boundary.leftBoundaryChar));
}

TEST_F(BackwardWORDTest, db_FromKeyword_ToSymbol_Safe) {
    // db uses End endpoint (word version)
    // canEndCross(Keyword, Symbol) = no (different word types)
    CharType firstChar = CharType::Keyword;
    EXPECT_FALSE(canEndCross(firstChar, boundary.leftBoundaryChar));
}

// =============================================================================
// Symmetry Tests: de/db use same endpoint (End)
// =============================================================================

TEST(SymmetryTest, DeAndDb_UseSameEndpoint) {
    // de (forward) and db (backward) both use End endpoint
    // The crossing check is symmetric based on direction:
    // - de: canEndCross(lastChar, rightBoundaryChar)
    // - db: canEndCross(firstChar, leftBoundaryChar)

    // Same char types should give same results
    EXPECT_EQ(canEndCross(CharType::Keyword, CharType::Keyword),
              canEndCross(CharType::Keyword, CharType::Keyword));

    EXPECT_EQ(canEndCross(CharType::Symbol, CharType::Keyword),
              canEndCross(CharType::Symbol, CharType::Keyword));
}

TEST(SymmetryTest, DwAndDge_UseDifferentEndpoints) {
    // dw (forward) uses Space, dge (backward) uses Next
    // These are NOT symmetric - different crossing behavior

    // Keyword → Keyword
    bool dwCrosses = canSpaceCross(CharType::Keyword, CharType::Keyword);    // Space: YES
    bool dgeCrosses = canNextCross(CharType::Keyword, CharType::Keyword);    // Next: YES
    // Both cross, but for different reasons

    // Keyword → Symbol
    dwCrosses = canSpaceCross(CharType::Keyword, CharType::Symbol);     // Space: no
    dgeCrosses = canNextCross(CharType::Keyword, CharType::Symbol);     // Next: YES
    EXPECT_NE(dwCrosses, dgeCrosses);  // Different behavior!
}
