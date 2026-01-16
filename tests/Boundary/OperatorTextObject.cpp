// tests/Boundary/OperatorTextObject.cpp
//
// Tests for operator + text object boundary crossing logic.
// Text objects select a range from cursor position in both directions.
//
// Text object semantics from boundary-logic.md:
//   diw/diW: (Backward, WordEdge) + (Forward, WordEdge)
//   daw/daW: Complex - depends on cursor position and trailing whitespace
//
// Unlike operator+motion which only checks one boundary (based on direction),
// text objects potentially cross BOTH left and right boundaries.

#include <gtest/gtest.h>

#include "BoundaryTestHelpers.h"
#include "VimCore/VimMovementUtils.h"

#include <random>

using namespace std;

// =============================================================================
// OperatorTextObject - Tests for text object boundary crossing
// =============================================================================

class OperatorTextObjectTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { oracle_ = make_unique<NeovimOracle>(); }
    static void TearDownTestSuite() { oracle_.reset(); }
    static unique_ptr<NeovimOracle> oracle_;

    mt19937 rng{42};
};

unique_ptr<NeovimOracle> OperatorTextObjectTest::oracle_;

// =============================================================================
// Text Object Test Infrastructure
// =============================================================================

struct TextObjectSpec {
    string cmd;       // e.g., "diw", "daw"
    bool isInner;     // true for iw/iW, false for aw/aW
    bool isBigWord;   // true for W variants
};

const vector<TextObjectSpec>& getAllTextObjects() {
    static vector<TextObjectSpec> textObjects = {
        {"diw", true,  false},
        {"daw", false, false},
        {"diW", true,  true},
        {"daW", false, true},
    };
    return textObjects;
}

// =============================================================================
// Text Object Buffer Generation (uses RandomBufferTest struct)
// =============================================================================
//
// Text objects need single-line edit regions with cursor on a word character.
// This wrapper generates appropriate test cases while reusing the shared struct.

RandomBufferTest generateTextObjectBuffer(mt19937& rng, int numLines) {
    RandomBufferTest test;

    // Character pools for different types
    const string keywords = "abcdefghijklmnop";  // Exclude Q, Z reserved
    const string symbols = ".,;:";               // Exclude @, # reserved

    uniform_int_distribution<int> lineLen(10, 20);
    uniform_int_distribution<int> charTypeDist(0, 2);

    // Build lines with mixed content
    for (int i = 0; i < numLines; i++) {
        int len = lineLen(rng);
        string line;
        line.reserve(len);

        for (int j = 0; j < len; j++) {
            int charType = charTypeDist(rng);
            if (charType == 0) {
                line += keywords[rng() % keywords.size()];
            } else if (charType == 1) {
                line += symbols[rng() % symbols.size()];
            } else {
                line += ' ';
            }
        }
        test.lines.push_back(line);
    }

    // For text objects, use single-line edit region
    uniform_int_distribution<int> lineDist(0, numLines - 1);
    int editLine = lineDist(rng);
    int lineLen2 = test.lines[editLine].size();

    // Ensure line is long enough for meaningful edit region
    int minEditLen = 4;
    if (lineLen2 < minEditLen + 2) {
        test.lines[editLine] += string(minEditLen + 2 - lineLen2, 'x');
        lineLen2 = test.lines[editLine].size();
    }

    // Pick edit region with room for boundaries
    uniform_int_distribution<int> startDist(1, max(1, lineLen2 - minEditLen - 1));
    int editStart = startDist(rng);

    uniform_int_distribution<int> endDist(editStart + minEditLen - 1,
                                          min(lineLen2 - 2, editStart + 10));
    int editEnd = endDist(rng);

    test.editStartLine = editLine;
    test.editStartCol = editStart;
    test.editEndLine = editLine;
    test.editEndCol = editEnd;

    // Place reserved boundary chars
    uniform_int_distribution<int> typeDist(0, 2);
    CharType leftType = static_cast<CharType>(typeDist(rng));
    CharType rightType = static_cast<CharType>(typeDist(rng));

    auto reservedCharLeft = [](CharType type) -> char {
        switch (type) {
            case CharType::Keyword: return 'Q';
            case CharType::Symbol: return '@';
            case CharType::Whitespace: return '\t';
            default: return 'Q';
        }
    };
    auto reservedCharRight = [](CharType type) -> char {
        switch (type) {
            case CharType::Keyword: return 'Z';
            case CharType::Symbol: return '#';
            case CharType::Whitespace: return '\t';
            default: return 'Z';
        }
    };

    string& line = test.lines[editLine];
    line[editStart - 1] = reservedCharLeft(leftType);
    line[editEnd + 1] = reservedCharRight(rightType);

    // Random cursor position within edit region
    uniform_int_distribution<int> cursorDist(editStart, editEnd);
    test.cursorCol = cursorDist(rng);
    test.cursorLine = editLine;

    // Set boundary info
    test.hasLeftBoundary = true;
    test.hasRightBoundary = true;
    test.leftBoundaryPos = Position(editLine, editStart - 1);
    test.rightBoundaryPos = Position(editLine, editEnd + 1);
    test.boundary.leftBoundaryChar = leftType;
    test.boundary.rightBoundaryChar = rightType;

    // Store prefix/suffix for verification (flattened representation)
    // Prefix: all lines before editLine + beginning of editLine up to editStart
    // Suffix: rest of editLine after editEnd + all lines after editLine
    string prefix;
    for (int i = 0; i < editLine; i++) {
        prefix += test.lines[i] + '\n';
    }
    prefix += line.substr(0, editStart);
    test.prefix = prefix;

    string suffix = line.substr(editEnd + 1);
    for (int i = editLine + 1; i < numLines; i++) {
        suffix += '\n';
        suffix += test.lines[i];
    }
    test.suffix = suffix;

    // Context flags
    test.hasLinesAbove = (editLine > 0);
    test.hasLinesBelow = (editLine < numLines - 1);

    return test;
}

// =============================================================================
// Text Object Test Runner
// =============================================================================

bool runTextObjectTest(NeovimOracle& oracle, const TextObjectSpec& spec,
                       const RandomBufferTest& test, bool verbose = false) {
    // Get actual result from Neovim
    auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, spec.cmd);

    bool leftCrossed = test.hasLeftBoundary && leftBoundaryCrossed(test, result.lines);
    bool rightCrossed = test.hasRightBoundary && rightBoundaryCrossed(test, result.lines);

    // Predict using textObjectRange - caller compares to boundaries
    Position cursor(test.cursorLine, test.cursorCol);
    Range predictedRange = VimMovementUtils::textObjectRange(
        cursor,
        test.lines,
        spec.isInner,
        spec.isBigWord);

    // For left, reaches if range.start <= leftBoundary; for right, if range.end >= rightBoundary
    bool predictLeftCross = test.hasLeftBoundary && predictedRange.isValid() &&
        (predictedRange.start <= test.leftBoundaryPos);
    bool predictRightCross = test.hasRightBoundary && predictedRange.isValid() &&
        (predictedRange.end >= test.rightBoundaryPos);

    bool success = (predictLeftCross == leftCrossed) && (predictRightCross == rightCrossed);

    if (verbose && !success) {
        cerr << "\n=== TEXT OBJECT TEST FAILURE ===" << endl;
        cerr << "Command: " << spec.cmd << endl;
        cerr << "Input:" << endl;
        for (size_t i = 0; i < test.lines.size(); i++) {
            cerr << "  [" << i << "]: \"" << test.lines[i] << "\"" << endl;
        }
        cerr << "Cursor: (" << test.cursorLine << ", " << test.cursorCol << ")" << endl;
        cerr << "Edit region: (" << test.editStartLine << "," << test.editStartCol << ") to ("
             << test.editEndLine << "," << test.editEndCol << ")" << endl;
        cerr << "Left boundary: (" << test.leftBoundaryPos.line << "," << test.leftBoundaryPos.col << ")" << endl;
        cerr << "Right boundary: (" << test.rightBoundaryPos.line << "," << test.rightBoundaryPos.col << ")" << endl;
        cerr << "Prefix: \"" << test.prefix << "\"" << endl;
        cerr << "Suffix: \"" << test.suffix << "\"" << endl;
        cerr << "Result:" << endl;
        for (size_t i = 0; i < result.lines.size(); i++) {
            cerr << "  [" << i << "]: \"" << result.lines[i] << "\"" << endl;
        }
        cerr << "Left - Predicted: " << (predictLeftCross ? "CROSS" : "SAFE")
             << ", Actual: " << (leftCrossed ? "CROSS" : "SAFE") << endl;
        cerr << "Right - Predicted: " << (predictRightCross ? "CROSS" : "SAFE")
             << ", Actual: " << (rightCrossed ? "CROSS" : "SAFE") << endl;
    }

    return success;
}

// =============================================================================
// Section 1: Random Buffer Stress Tests
// =============================================================================

TEST_F(OperatorTextObjectTest, RandomBufferStress_SingleLine) {
    const int NUM_BUFFERS = 10;
    const auto& textObjects = getAllTextObjects();

    int total = 0, passed = 0;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        auto test = generateTextObjectBuffer(rng, 1);

        for (const auto& spec : textObjects) {
            total++;
            if (runTextObjectTest(*oracle_, spec, test, true)) {
                passed++;
            }
        }
    }

    cerr << "\n=== Text Object Random Buffer (Single-Line): " << passed << "/" << total << " ===" << endl;
    EXPECT_EQ(passed, total);
}

TEST_F(OperatorTextObjectTest, RandomBufferStress_MultiLine) {
    const int NUM_BUFFERS = 10;
    const auto& textObjects = getAllTextObjects();

    int total = 0, passed = 0;

    for (int i = 0; i < NUM_BUFFERS; i++) {
        uniform_int_distribution<int> linesDist(2, 4);
        auto test = generateTextObjectBuffer(rng, linesDist(rng));

        for (const auto& spec : textObjects) {
            total++;
            if (runTextObjectTest(*oracle_, spec, test, true)) {
                passed++;
            }
        }
    }

    cerr << "\n=== Text Object Random Buffer (Multi-Line): " << passed << "/" << total << " ===" << endl;
    EXPECT_EQ(passed, total);
}

// =============================================================================
// Section 2: Manual Edge Case Tests
// =============================================================================

TEST_F(OperatorTextObjectTest, Diw_InMiddleOfWord) {
    Lines lines = {"hello world"};
    auto result = oracle_->simulate(lines, 0, 2, "diw");  // Cursor on 'l' in hello
    // Should delete "hello", leaving " world"
    EXPECT_EQ(result.lines[0], " world");
}

TEST_F(OperatorTextObjectTest, Diw_OnWhitespace) {
    Lines lines = {"hello   world"};
    auto result = oracle_->simulate(lines, 0, 6, "diw");  // Cursor in whitespace
    // Should delete the whitespace
    EXPECT_EQ(result.lines[0], "helloworld");
}

TEST_F(OperatorTextObjectTest, Daw_WithTrailingWhitespace) {
    Lines lines = {"hello   world"};
    auto result = oracle_->simulate(lines, 0, 0, "daw");  // Cursor on 'h'
    // Should delete "hello   " (word + trailing whitespace)
    EXPECT_EQ(result.lines[0], "world");
}

TEST_F(OperatorTextObjectTest, Daw_WithNoTrailingWhitespace) {
    Lines lines = {"hello"};
    auto result = oracle_->simulate(lines, 0, 0, "daw");  // Cursor on 'h', no trailing
    // Should delete entire word
    EXPECT_EQ(result.lines[0], "");
}

TEST_F(OperatorTextObjectTest, Daw_LastWordWithLeadingWhitespace) {
    Lines lines = {"hello world"};
    auto result = oracle_->simulate(lines, 0, 6, "daw");  // Cursor on 'w' in world
    // Should delete " world" (leading whitespace + word, since no trailing)
    EXPECT_EQ(result.lines[0], "hello");
}

TEST_F(OperatorTextObjectTest, DiW_OnSymbol) {
    Lines lines = {"hello... world"};  // Note: space before world
    auto result = oracle_->simulate(lines, 0, 0, "diW");  // Cursor on 'h'
    // WORD treats "hello..." as one word (no whitespace in it)
    EXPECT_EQ(result.lines[0], " world");
}

TEST_F(OperatorTextObjectTest, DaW_WithTrailingWhitespace) {
    Lines lines = {"hello...  world"};
    auto result = oracle_->simulate(lines, 0, 0, "daW");  // Cursor on 'h'
    // Should delete "hello...  "
    EXPECT_EQ(result.lines[0], "world");
}

// =============================================================================
// Section 3: Boundary Checking Against Our Implementation
// =============================================================================

TEST_F(OperatorTextObjectTest, TextObjectRange_InnerWord) {
    // Test that textObjectRange correctly computes range for diw
    Lines lines = {"abc def ghi"};
    Position cursor(0, 4);  // On 'd' in "def"
    Position leftBoundary(0, 3);   // space before "def"
    Position rightBoundary(0, 7);  // space after "def"

    Range result = VimMovementUtils::textObjectRange(cursor, lines, true, false);

    // diw should NOT reach the boundaries (only selects "def")
    EXPECT_FALSE(result.start <= leftBoundary);
    EXPECT_FALSE(result.end >= rightBoundary);
}

TEST_F(OperatorTextObjectTest, TextObjectRange_AroundWord) {
    // Test that textObjectRange correctly computes range for daw
    Lines lines = {"abc def ghi"};
    Position cursor(0, 4);  // On 'd' in "def"
    Position leftBoundary(0, 2);   // 'c' in "abc"
    Position rightBoundary(0, 8);  // 'g' in "ghi"

    Range result = VimMovementUtils::textObjectRange(cursor, lines, false, false);

    // daw should NOT reach the boundaries (selects "def " with trailing space)
    EXPECT_FALSE(result.start <= leftBoundary);
    EXPECT_FALSE(result.end >= rightBoundary);
}

TEST_F(OperatorTextObjectTest, TextObjectRange_CrossesBoundary) {
    // Test where text object DOES cross a boundary
    Lines lines = {"abc"};
    Position cursor(0, 1);         // On 'b'
    Position leftBoundary(0, 0);   // 'a' - adjacent to word
    Position rightBoundary(0, 2);  // 'c' - adjacent to word

    Range result = VimMovementUtils::textObjectRange(cursor, lines, true, false);

    // diw on "abc" should reach both boundaries (selects entire word)
    EXPECT_TRUE(result.start <= leftBoundary);
    EXPECT_TRUE(result.end >= rightBoundary);
}
