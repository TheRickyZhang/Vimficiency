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

#include "Boundary/EditBoundary.h"
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
// Random Buffer Generation for Text Object Testing
// =============================================================================

struct TextObjectTestCase {
    Lines lines;

    // Edit region (inclusive) - where text objects should NOT escape
    int editStartLine, editStartCol;
    int editEndLine, editEndCol;

    // Cursor position (within edit region)
    int cursorLine, cursorCol;

    // Boundary positions (just OUTSIDE the edit region)
    Position leftBoundaryPos;
    Position rightBoundaryPos;

    // Boundary char types
    CharType leftBoundaryChar;
    CharType rightBoundaryChar;

    bool hasLeftBoundary;
    bool hasRightBoundary;

    // Content outside edit region (for verification)
    string prefix;
    string suffix;
};

// Generate random buffer with random edit region for text object testing
TextObjectTestCase generateTextObjectBuffer(mt19937& rng, int numLines) {
    TextObjectTestCase test;

    // Character pools for different types
    const string keywords = "abcdefghijklmnop";  // Exclude Q, Z reserved
    const string symbols = ".,;:";               // Exclude @, # reserved
    const string whitespace = "    ";

    uniform_int_distribution<int> lineLen(8, 20);
    uniform_int_distribution<int> charTypeDist(0, 2);

    // Build lines
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

    // For simplicity, use single-line edit region
    uniform_int_distribution<int> lineDist(0, numLines - 1);
    int editLine = lineDist(rng);
    int lineLen2 = test.lines[editLine].size();

    // Pick edit region (at least 4 chars to have meaningful content)
    int minEditLen = 4;
    if (lineLen2 < minEditLen + 2) {
        // Pad line if too short
        test.lines[editLine] += string(minEditLen + 2 - lineLen2, 'x');
        lineLen2 = test.lines[editLine].size();
    }

    uniform_int_distribution<int> startDist(1, max(1, lineLen2 - minEditLen - 1));
    int editStart = startDist(rng);

    uniform_int_distribution<int> endDist(editStart + minEditLen - 1, min(lineLen2 - 2, editStart + 10));
    int editEnd = endDist(rng);

    test.editStartLine = editLine;
    test.editStartCol = editStart;
    test.editEndLine = editLine;
    test.editEndCol = editEnd;

    // Place reserved boundary chars
    uniform_int_distribution<int> typeDist(0, 2);
    CharType leftType = static_cast<CharType>(typeDist(rng));
    CharType rightType = static_cast<CharType>(typeDist(rng));

    // Use reserved chars for boundaries
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
    if (editStart > 0) {
        line[editStart - 1] = reservedCharLeft(leftType);
    }
    if (editEnd < (int)line.size() - 1) {
        line[editEnd + 1] = reservedCharRight(rightType);
    }

    // Random cursor position within edit region
    uniform_int_distribution<int> cursorDist(editStart, editEnd);
    test.cursorCol = cursorDist(rng);
    test.cursorLine = editLine;

    // Set boundary info
    test.hasLeftBoundary = (editStart > 0);
    test.hasRightBoundary = (editEnd < (int)line.size() - 1);
    test.leftBoundaryChar = test.hasLeftBoundary ? leftType : CharType::Newline;
    test.rightBoundaryChar = test.hasRightBoundary ? rightType : CharType::Newline;

    if (test.hasLeftBoundary) {
        test.leftBoundaryPos = Position(editLine, editStart - 1);
    }
    if (test.hasRightBoundary) {
        test.rightBoundaryPos = Position(editLine, editEnd + 1);
    }

    // Store prefix/suffix for verification
    test.prefix = line.substr(0, editStart);
    test.suffix = (editEnd < (int)line.size() - 1) ? line.substr(editEnd + 1) : "";

    return test;
}

// =============================================================================
// Boundary Crossing Detection
// =============================================================================

// Check if left boundary was crossed (prefix changed)
bool leftBoundaryCrossed(const TextObjectTestCase& test, const Lines& result) {
    if (result.size() != test.lines.size()) return true;
    const string& resultLine = result[test.editStartLine];
    if (resultLine.size() < test.prefix.size()) return true;
    return resultLine.substr(0, test.prefix.size()) != test.prefix;
}

// Check if right boundary was crossed (suffix changed)
bool rightBoundaryCrossed(const TextObjectTestCase& test, const Lines& result) {
    if (result.size() != test.lines.size()) return true;
    const string& resultLine = result[test.editEndLine];
    if (resultLine.size() < test.suffix.size()) return true;
    return resultLine.substr(resultLine.size() - test.suffix.size()) != test.suffix;
}

// =============================================================================
// Text Object Test Runner
// =============================================================================

bool runTextObjectTest(NeovimOracle& oracle, const TextObjectSpec& spec,
                       const TextObjectTestCase& test, bool verbose = false) {
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
