#include "BoundaryTestHelpers.h"

#include "Boundary/BoundaryToMotionInfo.h"
#include "VimCore/VimEndpointUtils.h"

#include <iostream>

using namespace std;

// =============================================================================
// Motion specifications
// =============================================================================

const vector<MotionSpec>& getAllMotions() {
    static vector<MotionSpec> motions = {
        // Forward motions (check lastChar vs rightBoundary)
        {"de",  [](CharType c, CharType bc) { return canEndCross(c, bc); }, true},
        {"dw",  [](CharType c, CharType bc) { return canSpaceCross(c, bc); }, true},
        {"dE",  [](CharType c, CharType bc) { return canEndCrossWORD(c, bc); }, true},
        {"dW",  [](CharType c, CharType bc) { return canSpaceCrossWORD(c, bc); }, true},
        // Backward motions (check firstChar vs leftBoundary)
        {"db",  [](CharType c, CharType bc) { return canEndCross(c, bc); }, false},
        {"dge", [](CharType c, CharType bc) { return canNextCross(c, bc); }, false},
        {"dB",  [](CharType c, CharType bc) { return canEndCrossWORD(c, bc); }, false},
        {"dgE", [](CharType c, CharType bc) { return canNextCrossWORD(c, bc); }, false},
    };
    return motions;
}

// =============================================================================
// Helper functions
// =============================================================================

string charsFor(CharType type, int count) {
    char c;
    switch (type) {
        case CharType::Keyword: c = 'x'; break;
        case CharType::Whitespace: c = ' '; break;
        case CharType::Symbol: c = '.'; break;
        default: c = 'x';
    }
    return string(count, c);
}

const char* typeName(CharType ct) {
    switch (ct) {
        case CharType::Keyword: return "Keyword";
        case CharType::Whitespace: return "Whitespace";
        case CharType::Symbol: return "Symbol";
        case CharType::Newline: return "Newline";
    }
    return "?";
}

// =============================================================================
// Test case builder
// =============================================================================

BoundaryTestCase buildTestCase(CharType contentType, CharType boundaryType,
                            bool isForward, int numLines) {
    BoundaryTestCase tc;
    tc.contentType = contentType;
    tc.boundaryType = boundaryType;
    tc.isForward = isForward;

    string content = charsFor(contentType);
    string boundary = (boundaryType == CharType::Newline) ? "" : charsFor(boundaryType);

    if (numLines == 1) {
        // Single line
        tc.hasLinesAbove = false;
        tc.hasLinesBelow = false;

        if (isForward) {
            // Forward: cursor at content start, boundary after content
            tc.lines = {"QQ " + content + boundary + " ZZ"};
            tc.cursorLine = 0;
            tc.cursorCol = 3;  // Start of content after "QQ "
        } else {
            // Backward: cursor at content end, boundary before content
            tc.lines = {"QQ " + boundary + content + " ZZ"};
            tc.cursorLine = 0;
            tc.cursorCol = 3 + boundary.size() + content.size() - 1;
        }
    } else {
        // Multi-line: boundary is Newline (content at line edge)
        if (isForward) {
            tc.lines = {"QQ " + content, "yy ZZ"};
            tc.cursorLine = 0;
            tc.cursorCol = 3;
            tc.hasLinesAbove = false;
            tc.hasLinesBelow = true;
        } else {
            tc.lines = {"QQ yy", content + " ZZ"};
            tc.cursorLine = 1;
            tc.cursorCol = content.size() - 1;
            tc.hasLinesAbove = true;
            tc.hasLinesBelow = false;
        }
    }

    return tc;
}

// =============================================================================
// Crossing detection
// =============================================================================

static bool forwardCrossed(const BoundaryTestCase& tc, const Lines& result) {
    if (tc.hasLinesBelow) {
        // Multi-line: check if second line is intact
        return !(result.size() >= 2 && result[1] == "yy ZZ");
    }
    // Single-line: check suffix and boundary
    if (result.empty()) return true;
    const string& line = result[0];
    if (line.size() < 3 || line.substr(line.size() - 3) != " ZZ") return true;

    if (tc.boundaryType == CharType::Newline) return false;

    string boundary = charsFor(tc.boundaryType);
    if (line.size() < boundary.size() + 3) return true;
    return line.substr(line.size() - 3 - boundary.size(), boundary.size()) != boundary;
}

static bool backwardCrossed(const BoundaryTestCase& tc, const Lines& result) {
    if (tc.hasLinesAbove) {
        // Multi-line: check if first line is intact
        return !(result.size() >= 1 && result[0] == "QQ yy");
    }
    // Single-line: check prefix and boundary
    if (result.empty()) return true;
    const string& line = result[0];
    if (line.size() < 3 || line.substr(0, 3) != "QQ ") return true;

    if (tc.boundaryType == CharType::Newline) return false;

    string boundary = charsFor(tc.boundaryType);
    if (line.size() < 3 + boundary.size()) return true;
    return line.substr(3, boundary.size()) != boundary;
}

bool didCross(const BoundaryTestCase& tc, const Lines& result) {
    return tc.isForward ? forwardCrossed(tc, result) : backwardCrossed(tc, result);
}

// =============================================================================
// Prediction
// =============================================================================

bool predictCross(const MotionSpec& motion, const BoundaryTestCase& tc) {
    bool baseCross = motion.crossFn(tc.contentType, tc.boundaryType);

    // Multi-line adjustments: some motions cross line boundaries
    if (tc.isForward && tc.hasLinesBelow && tc.boundaryType == CharType::Newline) {
        // de/dE from whitespace at EOL crosses to next line
        if (tc.contentType == CharType::Whitespace &&
            (motion.cmd == "de" || motion.cmd == "dE")) {
            return true;
        }
    }

    if (!tc.isForward && tc.hasLinesAbove && tc.boundaryType == CharType::Newline) {
        // db/dB from whitespace at col 0 crosses to previous line
        if (tc.contentType == CharType::Whitespace &&
            (motion.cmd == "db" || motion.cmd == "dB")) {
            return true;
        }
        // dge/dgE always crosses to previous word end
        if (motion.cmd == "dge" || motion.cmd == "dgE") {
            return true;
        }
    }

    return baseCross;
}

// =============================================================================
// Test runner
// =============================================================================

bool runBoundaryTest(NeovimOracle& oracle, const MotionSpec& motion,
                  const BoundaryTestCase& tc, bool verbose) {
    // Skip mismatched direction
    if (motion.isForward != tc.isForward) return true;

    auto result = oracle.simulate(tc.lines, tc.cursorLine, tc.cursorCol, motion.cmd);

    bool predicted = predictCross(motion, tc);
    bool actual = didCross(tc, result.lines);

    if (verbose && predicted != actual) {
        cerr << "\n=== MISMATCH ===" << endl;
        cerr << "Motion: " << motion.cmd << " (" << (tc.isForward ? "forward" : "backward") << ")" << endl;
        cerr << "Content: " << typeName(tc.contentType) << ", Boundary: " << typeName(tc.boundaryType) << endl;
        cerr << "Multi-line: hasAbove=" << tc.hasLinesAbove << ", hasBelow=" << tc.hasLinesBelow << endl;
        cerr << "Input:" << endl;
        for (size_t i = 0; i < tc.lines.size(); i++) {
            cerr << "  [" << i << "]: \"" << tc.lines[i] << "\"" << endl;
        }
        cerr << "Cursor: line " << tc.cursorLine << ", col " << tc.cursorCol << endl;
        cerr << "Result:" << endl;
        for (size_t i = 0; i < result.lines.size(); i++) {
            cerr << "  [" << i << "]: \"" << result.lines[i] << "\"" << endl;
        }
        cerr << "Predicted: " << (predicted ? "CROSS" : "SAFE") << endl;
        cerr << "Actual: " << (actual ? "CROSS" : "SAFE") << endl;
    }

    return predicted == actual;
}

// =============================================================================
// Random buffer generation
// =============================================================================

// Reserved boundary chars (distinct for left vs right to avoid false matches)
//
// IMPORTANT: We use TAB ('\t') for whitespace boundaries instead of space.
// This is because random content uses only spaces, so tab uniquely identifies
// boundary positions. Without this, prefix/suffix string matching would give
// false positives when boundary type is whitespace and content also has spaces.
//
// Left boundaries use: Q, @, tab
// Right boundaries use: Z, #, tab
static char reservedCharLeft(CharType type) {
    switch (type) {
        case CharType::Keyword: return 'Q';
        case CharType::Symbol: return '@';
        case CharType::Whitespace: return '\t';
        default: return 'Q';
    }
}

static char reservedCharRight(CharType type) {
    switch (type) {
        case CharType::Keyword: return 'Z';
        case CharType::Symbol: return '#';
        case CharType::Whitespace: return '\t';
        default: return 'Z';
    }
}

// Generate random char of given type (excluding reserved chars Q, Z, @, #)
static char randomCharOfType(CharType type, mt19937& rng) {
    switch (type) {
        case CharType::Keyword: {
            // a-p (excluding Q and Z which are reserved)
            uniform_int_distribution<int> dist(0, 15);
            return 'a' + dist(rng);
        }
        case CharType::Symbol: {
            // .,;: (excluding @ and # which are reserved)
            const char symbols[] = ".,;:";
            uniform_int_distribution<size_t> dist(0, 3);
            return symbols[dist(rng)];
        }
        case CharType::Whitespace:
            return ' ';
        default:
            return 'x';
    }
}

// Generate random char of random type
static char randomChar(mt19937& rng) {
    uniform_int_distribution<int> typeDist(0, 2);
    CharType type = static_cast<CharType>(typeDist(rng));
    return randomCharOfType(type, rng);
}

string flattenLines(const Lines& lines) {
    string result;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i > 0) result += '\n';
        result += lines[i];
    }
    return result;
}

static Lines unflattenLines(const string& flat) {
    return Lines::unflatten(flat);
}

RandomBufferTest generateRandomBuffer(mt19937& rng, int numLines) {
    RandomBufferTest test;

    // Generate random line lengths (5-15 chars each)
    uniform_int_distribution<int> lenDist(5, 15);

    // Build flat buffer with random content
    string flat;
    for (int line = 0; line < numLines; line++) {
        if (line > 0) flat += '\n';
        int len = lenDist(rng);
        for (int i = 0; i < len; i++) {
            flat += randomChar(rng);
        }
    }

    // Pick random edit region (at least 2 chars, not at very edges)
    int flatLen = flat.size();
    uniform_int_distribution<int> startDist(1, max(1, flatLen - 4));
    int editStart = startDist(rng);

    uniform_int_distribution<int> endDist(editStart + 1, min(flatLen - 2, editStart + 10));
    int editEnd = endDist(rng);

    // Skip if edit region spans a newline boundary in problematic way
    // (for simplicity, regenerate if boundary lands on newline)
    if (editStart > 0 && flat[editStart - 1] == '\n') editStart++;
    if (editEnd < flatLen - 1 && flat[editEnd + 1] == '\n') editEnd--;
    if (editStart >= editEnd) {
        // Fallback to simple single-line case
        flat = "abcdefghij";
        editStart = 2;
        editEnd = 7;
    }

    // Determine boundary types and place reserved chars
    uniform_int_distribution<int> typeDist(0, 2);
    CharType leftType = static_cast<CharType>(typeDist(rng));
    CharType rightType = static_cast<CharType>(typeDist(rng));

    // Place reserved boundary chars at boundary positions
    if (editStart > 0) {
        flat[editStart - 1] = reservedCharLeft(leftType);
    }
    if (editEnd < (int)flat.size() - 1) {
        flat[editEnd + 1] = reservedCharRight(rightType);
    }

    // Convert to lines
    test.lines = unflattenLines(flat);

    // Calculate line/col positions for edit region
    auto flatToLineCol = [&flat](int flatIdx) -> pair<int, int> {
        int line = 0, col = 0;
        for (int i = 0; i < flatIdx; i++) {
            if (flat[i] == '\n') { line++; col = 0; }
            else col++;
        }
        return {line, col};
    };

    auto [startLine, startCol] = flatToLineCol(editStart);
    test.editStartLine = startLine;
    test.editStartCol = startCol;

    auto [endLine, endCol] = flatToLineCol(editEnd);
    test.editEndLine = endLine;
    test.editEndCol = endCol;

    // Random cursor within edit region
    uniform_int_distribution<int> cursorDist(editStart, editEnd);
    int cursorFlat = cursorDist(rng);
    // Ensure cursor doesn't land on newline
    while (cursorFlat < (int)flat.size() && flat[cursorFlat] == '\n') {
        cursorFlat++;
    }
    if (cursorFlat > editEnd) cursorFlat = editEnd;

    auto [curLine, curCol] = flatToLineCol(cursorFlat);
    test.cursorLine = curLine;
    test.cursorCol = curCol;

    // Set boundary info
    test.hasLeftBoundary = (editStart > 0);
    test.hasRightBoundary = (editEnd < (int)flat.size() - 1);
    test.boundary.leftBoundaryChar = test.hasLeftBoundary ? leftType : CharType::Newline;
    test.boundary.rightBoundaryChar = test.hasRightBoundary ? rightType : CharType::Newline;

    // Compute boundary positions
    if (test.hasLeftBoundary) {
        auto [l, c] = flatToLineCol(editStart - 1);
        test.leftBoundaryPos = Position(l, c);
    }
    if (test.hasRightBoundary) {
        auto [l, c] = flatToLineCol(editEnd + 1);
        test.rightBoundaryPos = Position(l, c);
    }

    // Content edge chars (for dw, dge, dW, dgE)
    test.contentFirstChar = getCharType(flat[editStart]);
    test.contentLastChar = getCharType(flat[editEnd]);

    // Chars adjacent to cursor (for db/de/dB/dE which check NEXT char)
    // These skip newlines since motions traverse across lines
    auto findPrevChar = [&](int idx) -> CharType {
        while (--idx >= 0 && flat[idx] == '\n') {}
        return idx >= 0 ? getCharType(flat[idx]) : CharType::Newline;
    };
    auto findNextChar = [&](int idx) -> CharType {
        while (++idx < (int)flat.size() && flat[idx] == '\n') {}
        return idx < (int)flat.size() ? getCharType(flat[idx]) : CharType::Newline;
    };
    test.charBeforeCursor = findPrevChar(cursorFlat);
    test.charAfterCursor = findNextChar(cursorFlat);

    // Multi-line context
    test.hasLinesAbove = (test.editStartLine > 0);
    test.hasLinesBelow = (test.editEndLine < (int)test.lines.size() - 1);

    // Store prefix/suffix for verification (everything outside edit region)
    test.prefix = (editStart > 0) ? flat.substr(0, editStart) : "";
    test.suffix = (editEnd < (int)flat.size() - 1) ? flat.substr(editEnd + 1) : "";

    return test;
}

// Check if prefix is intact in result (left boundary respected)
static bool prefixIntact(const string& prefix, const string& resultFlat) {
    if (prefix.empty()) return true;
    if (resultFlat.size() < prefix.size()) return false;
    return resultFlat.substr(0, prefix.size()) == prefix;
}

// Check if suffix is intact in result (right boundary respected)
static bool suffixIntact(const string& suffix, const string& resultFlat) {
    if (suffix.empty()) return true;
    if (resultFlat.size() < suffix.size()) return false;
    return resultFlat.substr(resultFlat.size() - suffix.size()) == suffix;
}

bool boundaryRespected(const RandomBufferTest& test, const Lines& result) {
    string resultFlat = flattenLines(result);
    // Both prefix and suffix must be intact for boundary to be respected
    return prefixIntact(test.prefix, resultFlat) && suffixIntact(test.suffix, resultFlat);
}

// Check specifically if right boundary was crossed (for forward motions)
bool rightBoundaryCrossed(const RandomBufferTest& test, const Lines& result) {
    string resultFlat = flattenLines(result);
    return !suffixIntact(test.suffix, resultFlat);
}

// Check specifically if left boundary was crossed (for backward motions)
bool leftBoundaryCrossed(const RandomBufferTest& test, const Lines& result) {
    string resultFlat = flattenLines(result);
    return !prefixIntact(test.prefix, resultFlat);
}

// Predict if motion would cross IF it reaches the boundary
bool predictCrossRandom(const MotionSpec& motion, const RandomBufferTest& test) {
    CharType boundaryChar = motion.isForward ?
        test.boundary.rightBoundaryChar : test.boundary.leftBoundaryChar;

    // db/de/dB/dE check the NEXT char (cursor-1 or cursor+1), not the edge char
    // dw/dge/dW/dgE check the edge char of content
    CharType checkChar;
    if (motion.cmd == "db" || motion.cmd == "dB") {
        checkChar = test.charBeforeCursor;
    } else if (motion.cmd == "de" || motion.cmd == "dE") {
        checkChar = test.charAfterCursor;
    } else if (motion.isForward) {
        checkChar = test.contentLastChar;
    } else {
        checkChar = test.contentFirstChar;
    }

    bool baseCross = motion.crossFn(checkChar, boundaryChar);

    // Multi-line adjustments: some motions cross line boundaries
    if (motion.isForward && test.hasLinesBelow && boundaryChar == CharType::Newline) {
        if (checkChar == CharType::Whitespace &&
            (motion.cmd == "de" || motion.cmd == "dE")) {
            return true;
        }
    }

    if (!motion.isForward && test.hasLinesAbove && boundaryChar == CharType::Newline) {
        if (checkChar == CharType::Whitespace &&
            (motion.cmd == "db" || motion.cmd == "dB")) {
            return true;
        }
        if (motion.cmd == "dge" || motion.cmd == "dgE") {
            return true;
        }
    }

    return baseCross;
}

bool runRandomTest(NeovimOracle& oracle, const MotionSpec& motion,
                   const RandomBufferTest& test, bool verbose) {
    // Get EditInfo for this command
    auto editInfoOpt = getEditInfo(motion.cmd);
    if (!editInfoOpt) {
        cerr << "Unknown motion: " << motion.cmd << endl;
        return false;
    }
    const EditInfo& info = *editInfoOpt;

    // Determine which boundary to check
    bool hasBoundary = motion.isForward ? test.hasRightBoundary : test.hasLeftBoundary;
    Position boundaryPos = motion.isForward ? test.rightBoundaryPos : test.leftBoundaryPos;

    // Predict using motionWordEndpoint - caller compares to boundary
    bool predicted;
    if (!hasBoundary) {
        // No boundary to cross (at edge of buffer)
        predicted = false;
    } else {
        Position cursor(test.cursorLine, test.cursorCol);
        Position endpoint = VimEndpointUtils::motionWordEndpoint(
          cursor, test.lines,
          info.isForward, info.edgeType, info.isWORD, info.skipCurrent
        );
        // For forward motion, reaches if endpoint >= boundary; for backward, if endpoint <= boundary
        predicted = motion.isForward ? (endpoint >= boundaryPos) : (endpoint <= boundaryPos);
    }

    // Get actual result from Neovim
    auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, motion.cmd);
    bool actual = motion.isForward ?
        rightBoundaryCrossed(test, result.lines) :
        leftBoundaryCrossed(test, result.lines);

    // Strict comparison: predicted must match actual exactly
    bool success = (predicted == actual);

    if (verbose && !success) {
        cerr << "\n=== RANDOM TEST FAILURE ===" << endl;
        cerr << "Motion: " << motion.cmd << " (" << (motion.isForward ? "forward" : "backward") << ")" << endl;
        cerr << "Has boundary: " << (hasBoundary ? "yes" : "no") << endl;
        if (hasBoundary) {
            cerr << "Boundary pos: (" << boundaryPos.line << "," << boundaryPos.col << ")" << endl;
            cerr << "Boundary char: " << typeName(motion.isForward ?
                test.boundary.rightBoundaryChar : test.boundary.leftBoundaryChar) << endl;
        }
        cerr << "Input:" << endl;
        for (size_t i = 0; i < test.lines.size(); i++) {
            cerr << "  [" << i << "]: \"" << test.lines[i] << "\"" << endl;
        }
        cerr << "Edit region: (" << test.editStartLine << "," << test.editStartCol << ") to ("
             << test.editEndLine << "," << test.editEndCol << ")" << endl;
        cerr << "Cursor: (" << test.cursorLine << "," << test.cursorCol << ")" << endl;
        cerr << "Prefix: \"" << test.prefix << "\"" << endl;
        cerr << "Suffix: \"" << test.suffix << "\"" << endl;
        cerr << "Result:" << endl;
        for (size_t i = 0; i < result.lines.size(); i++) {
            cerr << "  [" << i << "]: \"" << result.lines[i] << "\"" << endl;
        }
        cerr << "Predicted: " << (predicted ? "EXTENDS_TOO_FAR" : "SAFE") << endl;
        cerr << "Actual: " << (actual ? "CROSSED" : "DID_NOT_CROSS") << endl;
    }

    return success;
}
