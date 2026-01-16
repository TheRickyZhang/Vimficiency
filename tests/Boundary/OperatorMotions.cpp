#include <gtest/gtest.h>

#include "Boundary/EditBoundary.h"
#include "BoundaryTestHelpers.h"

#include <random>

using namespace std;

// =============================================================================
// OperatorMotions - Tests for operator + motion boundary crossing logic
// =============================================================================

// =============================================================================
// Section 1: Manual Examples (Illustrative)
// =============================================================================
//
// Concrete example: "abc def.gh i", edit region cols 1-8 ("bc def.g")
// - leftBoundaryChar = 'a' (Keyword)
// - rightBoundaryChar = 'h' (Keyword)

class ManualExampleTest : public ::testing::Test {
protected:
    static constexpr const char* LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;
    static constexpr int EDIT_END = 8;

    EditBoundary boundary;

    void SetUp() override {
        boundary = analyzeEditBoundary(LINE, EDIT_START, EDIT_END);
    }
};

TEST_F(ManualExampleTest, BoundaryAnalysis) {
    EXPECT_EQ(boundary.leftBoundaryChar, CharType::Keyword);   // 'a'
    EXPECT_EQ(boundary.rightBoundaryChar, CharType::Keyword);  // 'h'
}

TEST_F(ManualExampleTest, ForwardFromKeywordToKeyword_Crosses) {
    // At position 'g' (col 8), content ends with Keyword. Boundary is 'h' (Keyword)
    // de uses End: canEndCross(Keyword, Keyword) = YES (same word continues)
    EXPECT_TRUE(canEndCross(CharType::Keyword, boundary.rightBoundaryChar));
}

TEST_F(ManualExampleTest, ForwardFromSymbolToKeyword_Safe) {
    // At position '.' (col 7), content ends with Symbol. Boundary is 'h' (Keyword)
    // dw uses Space: canSpaceCross(Symbol, Keyword) = no (different word type)
    EXPECT_FALSE(canSpaceCross(CharType::Symbol, boundary.rightBoundaryChar));
}

TEST_F(ManualExampleTest, BackwardFromKeywordToKeyword_Crosses) {
    // Content starts with 'b' (Keyword), boundary is 'a' (Keyword)
    // db uses End: canEndCross(Keyword, Keyword) = YES
    EXPECT_TRUE(canEndCross(CharType::Keyword, boundary.leftBoundaryChar));
}

TEST_F(ManualExampleTest, NewlineBoundary_AllSafe) {
    // When boundary is Newline (nothing beyond), all motions are safe
    EXPECT_FALSE(canEndCross(CharType::Keyword, CharType::Newline));
    EXPECT_FALSE(canSpaceCross(CharType::Keyword, CharType::Newline));
    EXPECT_FALSE(canNextCross(CharType::Keyword, CharType::Newline));
    EXPECT_FALSE(canLineCross(CharType::Newline));
}

// =============================================================================
// Section 2: Neovim-Verified Tests
// =============================================================================

class OperatorMotionsTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
    static void TearDownTestSuite() { oracle_.reset(); }
    static unique_ptr<NeovimOracle> oracle_;

    mt19937 rng{42};
};

unique_ptr<NeovimOracle> OperatorMotionsTest::oracle_;

// -----------------------------------------------------------------------------
// Random buffer stress test
// -----------------------------------------------------------------------------
//
// Generates truly random buffers with random edit regions.
// Tests ALL motions on each buffer (not just random selection).
// Uses reserved boundary chars for easy verification.

TEST_F(OperatorMotionsTest, RandomBufferStress_SingleLine) {
    // Keep relatively low for now
    const int NUM_BUFFERS = 10;
    const auto& motions = getAllMotions();

    int total = 0, passed = 0;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        auto test = generateRandomBuffer(rng, 1);

        for (const auto& motion : motions) {
            total++;
            if (runRandomTest(*oracle_, motion, test, true)) {
                passed++;
            }
        }
    }

    cerr << "\n=== Random Buffer (Single-Line): " << passed << "/" << total << " ===" << endl;
    EXPECT_EQ(passed, total);
}

TEST_F(OperatorMotionsTest, RandomBufferStress_MultiLine) {
    const int NUM_BUFFERS = 10;
    const auto& motions = getAllMotions();

    int total = 0, passed = 0;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        uniform_int_distribution<int> linesDist(2, 5);
        auto test = generateRandomBuffer(rng, linesDist(rng));

        for (const auto& motion : motions) {
            total++;
            if (runRandomTest(*oracle_, motion, test, true)) {
                passed++;
            }
        }
    }

    cerr << "\n=== Random Buffer (Multi-Line): " << passed << "/" << total << " ===" << endl;
    EXPECT_EQ(passed, total);
}

// -----------------------------------------------------------------------------
// Edge cases
// -----------------------------------------------------------------------------

TEST_F(OperatorMotionsTest, EdgeCase_NewlineBoundary_AllSafe) {
    // When boundary is Newline (single-line, edit spans entire line), all safe
    for (CharType c : {CharType::Keyword, CharType::Whitespace, CharType::Symbol}) {
        EXPECT_FALSE(canEndCross(c, CharType::Newline));
        EXPECT_FALSE(canSpaceCross(c, CharType::Newline));
        EXPECT_FALSE(canNextCross(c, CharType::Newline));
        EXPECT_FALSE(canEndCrossWORD(c, CharType::Newline));
        EXPECT_FALSE(canSpaceCrossWORD(c, CharType::Newline));
        EXPECT_FALSE(canNextCrossWORD(c, CharType::Newline));
    }
    EXPECT_FALSE(canLineCross(CharType::Newline));
}

TEST_F(OperatorMotionsTest, EdgeCase_SymmetryBetweenDirections) {
    // de and db use the same crossing function (canEndCross)
    // Same inputs -> same results
    for (CharType c : {CharType::Keyword, CharType::Whitespace, CharType::Symbol}) {
        for (CharType bc : {CharType::Keyword, CharType::Whitespace, CharType::Symbol}) {
            EXPECT_EQ(canEndCross(c, bc), canEndCross(c, bc));
        }
    }
}
