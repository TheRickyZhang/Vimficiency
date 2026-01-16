#pragma once

#include "Boundary/EditBoundary.h"
#include "Editor/Position.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"

#include <functional>
#include <random>
#include <string>
#include <vector>

// =============================================================================
// BoundaryTest Infrastructure
// =============================================================================
//
// Helpers for testing edit boundary crossing logic against Neovim.
//
// Key concepts:
// - EditBoundary stores what's OUTSIDE the edit region (left/right boundary chars)
// - Crossing functions: (edgeChar, boundaryChar) -> would motion cross?
// - Forward motions: check (lastChar, rightBoundaryChar)
// - Backward motions: check (firstChar, leftBoundaryChar)

// =============================================================================
// Motion specifications
// =============================================================================

struct MotionSpec {
    std::string cmd;
    std::function<bool(CharType, CharType)> crossFn;
    bool isForward;
};

// All motions we test (forward and backward)
const std::vector<MotionSpec>& getAllMotions();

// =============================================================================
// Test case structure
// =============================================================================

struct BoundaryTestCase {
    Lines lines;
    int cursorLine;
    int cursorCol;
    CharType contentType;
    CharType boundaryType;
    bool isForward;
    bool hasLinesAbove;  // For backward: are there lines above edit region?
    bool hasLinesBelow;  // For forward: are there lines below edit region?
};

// =============================================================================
// Helper functions
// =============================================================================

// Generate representative string for a CharType (e.g., "xx" for Keyword)
std::string charsFor(CharType type, int count = 2);

// Human-readable name for CharType
const char* typeName(CharType ct);

// =============================================================================
// Test case builder
// =============================================================================
//
// Builds minimal test cases for crossing verification.
//
// Construction rationale:
// - 2 chars of content ensures word motions have something to operate on
// - 2 chars of boundary ensures we can detect partial crossing
// - Markers "QQ " / " ZZ" are space-separated to avoid merging with any type
//
// numLines=1: Single line [prefix][boundary][content][boundary][suffix]
// numLines=2: Content at line edge, tests hasLinesAbove/Below

BoundaryTestCase buildTestCase(CharType contentType, CharType boundaryType,
                            bool isForward, int numLines = 1);

// =============================================================================
// Crossing detection
// =============================================================================

// Check if motion crossed the boundary by examining result
bool didCross(const BoundaryTestCase& tc, const Lines& result);

// Predict if motion should cross (including multi-line adjustments)
bool predictCross(const MotionSpec& motion, const BoundaryTestCase& tc);

// =============================================================================
// Test runner
// =============================================================================

// Run a single test case against Neovim, return true if prediction matches
bool runBoundaryTest(NeovimOracle& oracle, const MotionSpec& motion,
                  const BoundaryTestCase& tc, bool verbose = false);

// =============================================================================
// Random buffer generation (for stress tests)
// =============================================================================

struct RandomBufferTest {
    Lines lines;

    // Edit region (inclusive)
    int editStartLine, editStartCol;
    int editEndLine, editEndCol;

    // Cursor position (within edit region)
    int cursorLine, cursorCol;

    // Boundary positions (just OUTSIDE the edit region)
    // For forward motions: check against rightBoundaryPos
    // For backward motions: check against leftBoundaryPos
    Position leftBoundaryPos;   // Position of char just before edit start
    Position rightBoundaryPos;  // Position of char just after edit end

    // Computed from buffer
    EditBoundary boundary;
    CharType contentFirstChar;  // For backward motions (dw, dge, dW, dgE)
    CharType contentLastChar;   // For forward motions (dw, dge, dW, dgE)

    // For db/de/dB/dE: these check the NEXT char, not current
    CharType charBeforeCursor;  // For db/dB (char at cursor-1)
    CharType charAfterCursor;   // For de/dE (char at cursor+1)

    // Context
    bool hasLinesAbove;
    bool hasLinesBelow;
    bool hasLeftBoundary;   // Is there a char before edit region?
    bool hasRightBoundary;  // Is there a char after edit region?

    // Content outside edit region (for verification)
    std::string prefix;  // Everything before edit region
    std::string suffix;  // Everything after edit region
};

// Generate random buffer with random edit region
// numLines: number of lines in buffer (1-N)
// rng: random number generator
RandomBufferTest generateRandomBuffer(std::mt19937& rng, int numLines);

// Flatten lines to single string with \n separators
std::string flattenLines(const Lines& lines);

// Check if content outside edit region is unchanged
bool boundaryRespected(const RandomBufferTest& test,
                       const Lines& result);

// Check if specific boundary was crossed (for text objects that check both directions)
bool leftBoundaryCrossed(const RandomBufferTest& test, const Lines& result);
bool rightBoundaryCrossed(const RandomBufferTest& test, const Lines& result);

// Predict if motion would cross for random buffer test
bool predictCrossRandom(const MotionSpec& motion, const RandomBufferTest& test);

// Run random buffer test against Neovim
bool runRandomTest(NeovimOracle& oracle, const MotionSpec& motion,
                   const RandomBufferTest& test, bool verbose = false);
