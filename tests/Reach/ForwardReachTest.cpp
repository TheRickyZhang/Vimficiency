#include <gtest/gtest.h>

#include "Editor/Edit.h"
#include "Editor/Motion.h"
#include "Editor/Range.h"
#include "VimCore/VimEditUtils.h"
#include "Optimizer/EditBoundary.h"
#include "Utils/NeovimOracle.h"

#include <random>

using namespace std;

// =============================================================================
// Forward Reach Test Suite
// Tests that forward edit operations respect edit boundaries
// =============================================================================

// =============================================================================
// Hand-crafted tests (original examples)
// =============================================================================

class ForwardReachTest : public ::testing::Test {
protected:
    static constexpr const char* FULL_LINE = "abc def.gh i";
    static constexpr int EDIT_START = 1;  // 'b'
    static constexpr int EDIT_END = 8;    // 'g' (inclusive)

    string applySimpleEdit(const string& line, int cursorCol, const string& editCmd) {
        Lines lines = {line};
        Position pos(0, cursorCol);
        Mode mode = Mode::Normal;
        NavContext nav(40, 20);
        Edit::applyEdit(lines, pos, mode, nav, ParsedEdit(editCmd));
        return lines[0];
    }

    string applyOperatorMotion(const string& line, int cursorCol, const string& motion) {
        Lines lines = {line};
        Position startPos(0, cursorCol);
        Mode mode = Mode::Normal;
        NavContext nav(40, 20);

        MotionResult result = simulateMotions(startPos, mode, nav, motion, lines);
        Position endPos = result.pos;
        bool inclusive = (motion == "e" || motion == "E" || motion == "ge" || motion == "gE");
        Range range = rangeFromMotion(startPos, endPos, inclusive);

        if (!inclusive && range.end.col > 0) {
            range.end.col--;
            range.inclusive = true;
        }

        VimEditUtils::deleteRange(lines, range, startPos);
        return lines[0];
    }

    bool isEditSafe(const string& original, const string& result, int editStart, int editEnd) {
        for (int i = 0; i < editStart && i < (int)original.size() && i < (int)result.size(); i++) {
            if (original[i] != result[i]) return false;
        }
        int origAfterEdit = editEnd + 1;
        string origSuffix = original.substr(origAfterEdit);
        if (result.size() < origSuffix.size()) return false;
        string resultSuffix = result.substr(result.size() - origSuffix.size());
        return origSuffix == resultSuffix;
    }
};

TEST_F(ForwardReachTest, AtG_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 8, "x");
    EXPECT_EQ(result, "abc def.h i");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtG_De_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 8, "e");
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtDot_X_Works) {
    string result = applySimpleEdit(FULL_LINE, 7, "x");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtDot_Dw_Works) {
    string result = applyOperatorMotion(FULL_LINE, 7, "w");
    EXPECT_TRUE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtF_DW_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 6, "W");
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtSpace_DE_Fails) {
    string result = applyOperatorMotion(FULL_LINE, 3, "E");
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

TEST_F(ForwardReachTest, AtB_D_Fails) {
    string result = applySimpleEdit(FULL_LINE, 1, "D");
    EXPECT_FALSE(isEditSafe(FULL_LINE, result, EDIT_START, EDIT_END));
}

// =============================================================================
// Crossing Function Unit Tests
// =============================================================================

class CrossingFunctionTest : public ::testing::Test {
protected:
    EditBoundary keywordBoundary;
    EditBoundary whitespaceBoundary;
    EditBoundary symbolToKeywordBoundary;

    void SetUp() override {
        keywordBoundary = analyzeEditBoundary("abc def.gh i", 1, 8);
        whitespaceBoundary = analyzeEditBoundary("abc  def", 1, 3);
        symbolToKeywordBoundary = analyzeEditBoundary("abc.def", 0, 3);
    }
};

TEST_F(CrossingFunctionTest, BoundaryAnalysis) {
    EXPECT_EQ(keywordBoundary.rightBoundaryChar, CharType::Keyword);
    EXPECT_EQ(keywordBoundary.leftBoundaryChar, CharType::Keyword);
    EXPECT_EQ(whitespaceBoundary.rightBoundaryChar, CharType::Whitespace);
    EXPECT_EQ(symbolToKeywordBoundary.rightBoundaryChar, CharType::Keyword);
    EXPECT_EQ(symbolToKeywordBoundary.leftBoundaryChar, CharType::Newline);
}

TEST_F(CrossingFunctionTest, EndCross_KeywordToKeyword_Crosses) {
    EXPECT_TRUE(canEndCross(CharType::Keyword, CharType::Keyword));
}

TEST_F(CrossingFunctionTest, EndCross_KeywordToOther_Safe) {
    EXPECT_FALSE(canEndCross(CharType::Keyword, CharType::Whitespace));
    EXPECT_FALSE(canEndCross(CharType::Keyword, CharType::Symbol));
    EXPECT_FALSE(canEndCross(CharType::Keyword, CharType::Newline));
}

TEST_F(CrossingFunctionTest, SpaceCross_KeywordToKeyword_Crosses) {
    EXPECT_TRUE(canSpaceCross(CharType::Keyword, CharType::Keyword));
}

TEST_F(CrossingFunctionTest, SpaceCross_KeywordToWhitespace_Crosses) {
    EXPECT_TRUE(canSpaceCross(CharType::Keyword, CharType::Whitespace));
}

TEST_F(CrossingFunctionTest, SpaceCross_KeywordToSymbol_Safe) {
    EXPECT_FALSE(canSpaceCross(CharType::Keyword, CharType::Symbol));
}

TEST_F(CrossingFunctionTest, LineCross) {
    EXPECT_TRUE(canLineCross(CharType::Keyword));
    EXPECT_TRUE(canLineCross(CharType::Whitespace));
    EXPECT_FALSE(canLineCross(CharType::Newline));
}

TEST_F(CrossingFunctionTest, EndCrossWORD_NonWSToNonWS_Crosses) {
    EXPECT_TRUE(canEndCrossWORD(CharType::Keyword, CharType::Keyword));
    EXPECT_TRUE(canEndCrossWORD(CharType::Keyword, CharType::Symbol));
    EXPECT_TRUE(canEndCrossWORD(CharType::Symbol, CharType::Keyword));
}

TEST_F(CrossingFunctionTest, SpaceCrossWORD) {
    EXPECT_TRUE(canSpaceCrossWORD(CharType::Keyword, CharType::Keyword));
    EXPECT_TRUE(canSpaceCrossWORD(CharType::Keyword, CharType::Whitespace));
    EXPECT_TRUE(canSpaceCrossWORD(CharType::Whitespace, CharType::Whitespace));
    EXPECT_FALSE(canSpaceCrossWORD(CharType::Whitespace, CharType::Keyword));
}

// =============================================================================
// NeovimOracle-backed Testing
// =============================================================================
//
// Strategy:
// The crossing functions predict: "if you execute this motion when positioned
// at content ending with lastChar, with boundaryChar immediately following,
// would the motion cross into the boundary?"
//
// To test this, we create minimal test cases:
// - For forward motions: [content][boundary] where cursor is at the last char of content
// - For backward motions: [boundary][content] where cursor is in the content
//
// We then run the motion and check if the boundary was modified.

class NeovimCrossingTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        oracle_ = std::make_unique<NeovimOracle>();
    }
    static void TearDownTestSuite() {
        oracle_.reset();
    }
    static std::unique_ptr<NeovimOracle> oracle_;

    // Random generator (seeded for reproducibility)
    std::mt19937 rng{42};

    // Detailed debug output
    void printDebug(const string& label, const string& fullLine, int cursorCol,
                    const string& op, const string& resultLine,
                    bool predictedSafe, bool actualSafe,
                    CharType edgeChar, CharType boundaryChar) {
        cerr << "\n" << string(60, '=') << endl;
        cerr << label << endl;
        cerr << string(60, '-') << endl;
        cerr << "Full line:      \"" << fullLine << "\"" << endl;
        cerr << "Cursor:         col " << cursorCol << " ('" << fullLine[cursorCol] << "')" << endl;
        cerr << "Operation:      " << op << endl;
        cerr << "Result:         \"" << resultLine << "\"" << endl;
        cerr << "Edge char:      " << charTypeName(edgeChar) << endl;
        cerr << "Boundary char:  " << charTypeName(boundaryChar) << endl;
        cerr << "Prediction:     " << (predictedSafe ? "SAFE" : "CROSSES") << endl;
        cerr << "Actual:         " << (actualSafe ? "SAFE" : "CROSSES") << endl;
        cerr << "Status:         " << (predictedSafe == actualSafe ? "PASS" : "*** MISMATCH ***") << endl;
        cerr << string(60, '=') << endl;
    }

    static const char* charTypeName(CharType ct) {
        switch (ct) {
            case CharType::Keyword: return "Keyword";
            case CharType::Whitespace: return "Whitespace";
            case CharType::Symbol: return "Symbol";
            case CharType::Newline: return "Newline";
        }
        return "Unknown";
    }

    // Generate representative chars for each type
    char charOfType(CharType type) {
        switch (type) {
            case CharType::Keyword: return 'x';
            case CharType::Whitespace: return ' ';
            case CharType::Symbol: return '.';
            default: return 'x';
        }
    }

    // =======================================================================
    // Forward Test Structure
    // =======================================================================
    // The crossing functions predict: when a motion reaches its natural stopping
    // point, would it cross into the boundary?
    //
    // For `de`:
    //   - Stops at word end (inclusive)
    //   - If content and boundary are same word type, motion continues through
    //   - E.g., "xxyy.ZZ" with cursor on first 'x': de goes to 'y' end (crosses into "yy")
    //
    // For `dw`:
    //   - Stops at next word start (exclusive) + trailing whitespace
    //   - E.g., "xx  yy" with cursor on first 'x': dw deletes "xx  " → crosses if boundary is ' '
    //
    // Test structure:
    //   [content][boundary][suffix_marker]
    //   - content: 2+ chars of lastCharType
    //   - boundary: 2+ chars of boundaryType
    //   - suffix_marker: ".ZZ" - a different type to clearly separate
    //   - Cursor at START of content
    //   - Check if suffix_marker is at the expected position after motion

    struct ForwardTestCase {
        string line;
        int cursorCol;
        CharType lastChar;
        CharType boundaryChar;
    };

    ForwardTestCase buildForwardTest(CharType lastCharType, CharType boundaryType) {
        ForwardTestCase tc;
        tc.lastChar = lastCharType;
        tc.boundaryChar = boundaryType;

        // Build content (2 chars of lastCharType)
        string content;
        switch (lastCharType) {
            case CharType::Keyword: content = "xx"; break;
            case CharType::Whitespace: content = "  "; break;
            case CharType::Symbol: content = ".."; break;
            default: content = "xx";
        }

        // Build boundary (2 chars of boundaryType)
        string boundary;
        switch (boundaryType) {
            case CharType::Keyword: boundary = "yy"; break;
            case CharType::Whitespace: boundary = "  "; break;
            case CharType::Symbol: boundary = ",,"; break;
            default: boundary = "yy";
        }

        // Suffix marker: " QQ" (space + keyword) - space guarantees separation from any boundary type
        string suffix = " QQ";

        tc.line = content + boundary + suffix;
        tc.cursorCol = 0;  // Start of content

        return tc;
    }

    // Check if forward motion crossed the boundary
    // Safe = boundary chars are still present and in correct position
    bool forwardMotionCrossed(const ForwardTestCase& tc, const string& result) {
        // The suffix " QQ" should always be at the end
        if (result.size() < 3) return true;  // Definitely crossed
        if (result.substr(result.size() - 3) != " QQ") return true;  // Suffix damaged

        // Now check if boundary was eaten
        // Original: content + boundary + " QQ"
        // After safe motion: some_prefix + boundary + " QQ"
        // After crossing motion: some_prefix + partial_boundary + " QQ" or just " QQ"

        // Build expected boundary string
        string expectedBoundary;
        switch (tc.boundaryChar) {
            case CharType::Keyword: expectedBoundary = "yy"; break;
            case CharType::Whitespace: expectedBoundary = "  "; break;
            case CharType::Symbol: expectedBoundary = ",,"; break;
            default: expectedBoundary = "yy";
        }

        // Check if boundary is intact before suffix
        if (result.size() < expectedBoundary.size() + 3) return true;
        string beforeSuffix = result.substr(result.size() - 3 - expectedBoundary.size(), expectedBoundary.size());
        return beforeSuffix != expectedBoundary;
    }

    // =======================================================================
    // Backward Test Structure
    // =======================================================================
    // For backward motions, we reverse the structure:
    //   [prefix_marker][boundary][content]
    //   - Cursor at END of content
    //   - Check if boundary is preserved

    struct BackwardTestCase {
        string line;
        int cursorCol;
        CharType firstChar;
        CharType boundaryChar;
    };

    BackwardTestCase buildBackwardTest(CharType firstCharType, CharType boundaryType) {
        BackwardTestCase tc;
        tc.firstChar = firstCharType;
        tc.boundaryChar = boundaryType;

        // Prefix marker: "QQ " (keyword + space) - space guarantees separation from any boundary type
        string prefix = "QQ ";

        // Build boundary (2 chars of boundaryType)
        string boundary;
        switch (boundaryType) {
            case CharType::Keyword: boundary = "yy"; break;
            case CharType::Whitespace: boundary = "  "; break;
            case CharType::Symbol: boundary = ",,"; break;
            default: boundary = "yy";
        }

        // Build content (2 chars of firstCharType)
        string content;
        switch (firstCharType) {
            case CharType::Keyword: content = "xx"; break;
            case CharType::Whitespace: content = "  "; break;
            case CharType::Symbol: content = ".."; break;
            default: content = "xx";
        }

        tc.line = prefix + boundary + content;
        tc.cursorCol = tc.line.size() - 1;  // End of content

        return tc;
    }

    // Check if backward motion crossed the boundary
    bool backwardMotionCrossed(const BackwardTestCase& tc, const string& result) {
        // The prefix "QQ " should always be at the start
        if (result.size() < 3) return true;
        if (result.substr(0, 3) != "QQ ") return true;

        // Build expected boundary string
        string expectedBoundary;
        switch (tc.boundaryChar) {
            case CharType::Keyword: expectedBoundary = "yy"; break;
            case CharType::Whitespace: expectedBoundary = "  "; break;
            case CharType::Symbol: expectedBoundary = ",,"; break;
            default: expectedBoundary = "yy";
        }

        // Check if boundary is intact after prefix
        if (result.size() < 3 + expectedBoundary.size()) return true;
        string afterPrefix = result.substr(3, expectedBoundary.size());
        return afterPrefix != expectedBoundary;
    }
};

std::unique_ptr<NeovimOracle> NeovimCrossingTest::oracle_;

// =============================================================================
// Systematic Forward Motion Tests
// =============================================================================

TEST_F(NeovimCrossingTest, ForwardMotions_Systematic) {
    struct OpSpec {
        string op;
        function<bool(CharType, CharType)> crossFn;
    };

    vector<OpSpec> ops = {
        {"dw", [](CharType last, CharType bc) { return canSpaceCross(last, bc); }},
        {"de", [](CharType last, CharType bc) { return canEndCross(last, bc); }},
        {"dW", [](CharType last, CharType bc) { return canSpaceCrossWORD(last, bc); }},
        {"dE", [](CharType last, CharType bc) { return canEndCrossWORD(last, bc); }},
    };

    vector<CharType> edgeTypes = {CharType::Keyword, CharType::Whitespace, CharType::Symbol};
    vector<CharType> boundaryTypes = {CharType::Keyword, CharType::Whitespace, CharType::Symbol};

    int total = 0, passed = 0, failed = 0;

    for (const auto& opSpec : ops) {
        for (CharType lastChar : edgeTypes) {
            for (CharType boundaryChar : boundaryTypes) {
                ForwardTestCase tc = buildForwardTest(lastChar, boundaryChar);

                bool predictedCross = opSpec.crossFn(lastChar, boundaryChar);
                bool predictedSafe = !predictedCross;

                // Run in Neovim
                auto result = oracle_->simulate({tc.line}, 0, tc.cursorCol, opSpec.op);

                // Check if boundary was crossed
                bool actualCrossed = forwardMotionCrossed(tc, result.lines[0]);
                bool actualSafe = !actualCrossed;

                total++;
                if (predictedSafe == actualSafe) {
                    passed++;
                } else {
                    failed++;
                    printDebug("FORWARD MISMATCH: " + opSpec.op, tc.line, tc.cursorCol,
                               opSpec.op, result.lines[0], predictedSafe, actualSafe,
                               lastChar, boundaryChar);
                }
            }
        }
    }

    cerr << "\n=== Forward Motion Systematic Test ===" << endl;
    cerr << "Total: " << total << ", Passed: " << passed << ", Failed: " << failed << endl;

    EXPECT_EQ(failed, 0) << "Forward motion tests had " << failed << " failures";
}

// =============================================================================
// Systematic Backward Motion Tests
// =============================================================================

TEST_F(NeovimCrossingTest, BackwardMotions_Systematic) {
    struct OpSpec {
        string op;
        function<bool(CharType, CharType)> crossFn;
    };

    vector<OpSpec> ops = {
        {"db", [](CharType first, CharType bc) { return canEndCross(first, bc); }},
        {"dge", [](CharType first, CharType bc) { return canNextCross(first, bc); }},
        {"dB", [](CharType first, CharType bc) { return canEndCrossWORD(first, bc); }},
        {"dgE", [](CharType first, CharType bc) { return canNextCrossWORD(first, bc); }},
    };

    vector<CharType> edgeTypes = {CharType::Keyword, CharType::Whitespace, CharType::Symbol};
    vector<CharType> boundaryTypes = {CharType::Keyword, CharType::Whitespace, CharType::Symbol};

    int total = 0, passed = 0, failed = 0;

    for (const auto& opSpec : ops) {
        for (CharType firstChar : edgeTypes) {
            for (CharType boundaryChar : boundaryTypes) {
                BackwardTestCase tc = buildBackwardTest(firstChar, boundaryChar);

                bool predictedCross = opSpec.crossFn(firstChar, boundaryChar);
                bool predictedSafe = !predictedCross;

                // Run in Neovim
                auto result = oracle_->simulate({tc.line}, 0, tc.cursorCol, opSpec.op);

                // Check if boundary was crossed
                bool actualCrossed = backwardMotionCrossed(tc, result.lines[0]);
                bool actualSafe = !actualCrossed;

                total++;
                if (predictedSafe == actualSafe) {
                    passed++;
                } else {
                    failed++;
                    printDebug("BACKWARD MISMATCH: " + opSpec.op, tc.line, tc.cursorCol,
                               opSpec.op, result.lines[0], predictedSafe, actualSafe,
                               firstChar, boundaryChar);
                }
            }
        }
    }

    cerr << "\n=== Backward Motion Systematic Test ===" << endl;
    cerr << "Total: " << total << ", Passed: " << passed << ", Failed: " << failed << endl;

    EXPECT_EQ(failed, 0) << "Backward motion tests had " << failed << " failures";
}

// =============================================================================
// Random Stress Test with varied content
// =============================================================================

TEST_F(NeovimCrossingTest, RandomStressTest) {
    const int NUM_TESTS = 100;

    vector<pair<string, function<bool(CharType, CharType)>>> forwardOps = {
        {"dw", [](CharType e, CharType b) { return canSpaceCross(e, b); }},
        {"de", [](CharType e, CharType b) { return canEndCross(e, b); }},
        {"dW", [](CharType e, CharType b) { return canSpaceCrossWORD(e, b); }},
        {"dE", [](CharType e, CharType b) { return canEndCrossWORD(e, b); }},
    };

    vector<pair<string, function<bool(CharType, CharType)>>> backwardOps = {
        {"db", [](CharType e, CharType b) { return canEndCross(e, b); }},
        {"dge", [](CharType e, CharType b) { return canNextCross(e, b); }},
        {"dB", [](CharType e, CharType b) { return canEndCrossWORD(e, b); }},
        {"dgE", [](CharType e, CharType b) { return canNextCrossWORD(e, b); }},
    };

    vector<CharType> types = {CharType::Keyword, CharType::Whitespace, CharType::Symbol};

    int passed = 0, failed = 0;

    for (int i = 0; i < NUM_TESTS; i++) {
        uniform_int_distribution<int> dirDist(0, 1);
        bool isForward = dirDist(rng) == 0;

        uniform_int_distribution<size_t> typeDist(0, types.size() - 1);
        CharType edgeType = types[typeDist(rng)];
        CharType boundaryType = types[typeDist(rng)];

        bool predictedCross, predictedSafe;
        string op;
        string line, resultLine;
        int cursorCol;

        if (isForward) {
            uniform_int_distribution<size_t> opDist(0, forwardOps.size() - 1);
            auto& opPair = forwardOps[opDist(rng)];
            op = opPair.first;

            ForwardTestCase tc = buildForwardTest(edgeType, boundaryType);
            line = tc.line;
            cursorCol = tc.cursorCol;

            predictedCross = opPair.second(edgeType, boundaryType);
            predictedSafe = !predictedCross;

            auto result = oracle_->simulate({tc.line}, 0, tc.cursorCol, op);
            resultLine = result.lines[0];

            bool actualCrossed = forwardMotionCrossed(tc, resultLine);
            bool actualSafe = !actualCrossed;

            if (predictedSafe == actualSafe) {
                passed++;
            } else {
                failed++;
                printDebug("RANDOM #" + to_string(i), line, cursorCol, op,
                           resultLine, predictedSafe, actualSafe, edgeType, boundaryType);
            }
        } else {
            uniform_int_distribution<size_t> opDist(0, backwardOps.size() - 1);
            auto& opPair = backwardOps[opDist(rng)];
            op = opPair.first;

            BackwardTestCase tc = buildBackwardTest(edgeType, boundaryType);
            line = tc.line;
            cursorCol = tc.cursorCol;

            predictedCross = opPair.second(edgeType, boundaryType);
            predictedSafe = !predictedCross;

            auto result = oracle_->simulate({tc.line}, 0, tc.cursorCol, op);
            resultLine = result.lines[0];

            bool actualCrossed = backwardMotionCrossed(tc, resultLine);
            bool actualSafe = !actualCrossed;

            if (predictedSafe == actualSafe) {
                passed++;
            } else {
                failed++;
                printDebug("RANDOM #" + to_string(i), line, cursorCol, op,
                           resultLine, predictedSafe, actualSafe, edgeType, boundaryType);
            }
        }
    }

    cerr << "\n=== Random Stress Test ===" << endl;
    cerr << "Total: " << (passed + failed) << ", Passed: " << passed << ", Failed: " << failed << endl;

    EXPECT_EQ(failed, 0) << "Random stress test had " << failed << " failures";
}

// =============================================================================
// Original Example Verification with Neovim
// =============================================================================

TEST_F(NeovimCrossingTest, OriginalExample_VerifyWithNeovim) {
    // Verify the hand-crafted "abc def.gh i" example against Neovim
    const string fullLine = "abc def.gh i";
    const int editStart = 1;  // 'b'
    const int editEnd = 8;    // 'g'

    struct Case {
        int col;
        string op;
        bool expectSafe;
    };

    vector<Case> cases = {
        {8, "de", false},  // de from 'g' crosses to 'h'
        {7, "dw", true},   // dw from '.' stays within
        {7, "de", false},  // de from '.' crosses to 'h'
        {6, "dW", false},  // dW from 'f' crosses to 'h'
        {3, "dE", false},  // dE from ' ' crosses to 'h'
        {1, "D", false},   // D from 'b' crosses to end
    };

    for (const auto& c : cases) {
        auto result = oracle_->simulate({fullLine}, 0, c.col, c.op);

        string expectedSuffix = fullLine.substr(editEnd + 1);
        bool actualSafe = true;
        if (result.lines[0].size() < expectedSuffix.size()) {
            actualSafe = false;
        } else {
            string resultSuffix = result.lines[0].substr(result.lines[0].size() - expectedSuffix.size());
            actualSafe = (resultSuffix == expectedSuffix);
        }

        EXPECT_EQ(actualSafe, c.expectSafe)
            << "Op " << c.op << " at col " << c.col
            << ": expected " << (c.expectSafe ? "safe" : "cross")
            << ", got " << (actualSafe ? "safe" : "cross")
            << ". Result: \"" << result.lines[0] << "\"";
    }
}
