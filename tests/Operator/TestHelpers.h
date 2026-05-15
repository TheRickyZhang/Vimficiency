// tests/Operator/TestHelpers.h
//
// Operator-specific test infrastructure for boundary crossing tests.
// Builds on RandomBufferHelpers for core random generation.
// Provides boundary-aware buffer generation and crossing verification.

#pragma once

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/RandomBufferHelpers.h"  // Core random generation
#include "VimCore/VimEndpointUtils.h"

#include <string>
#include <vector>

// =============================================================================
// MotionSpec - Motion specification with VimCore-based prediction
// =============================================================================

struct MotionSpec {
  std::string cmd;   // e.g., "de", "dw", "db"
  bool isForward;    // direction of motion
  VimCore::WordOperatorTarget target;
  bool isBigWord;    // true for W/E/B variants

  // Predict if motion would cross boundary using VimCore
  // boundaryOffset: for forward, protects suffix (last N cols of last line)
  //                 for backward, protects prefix (first N cols of line 0)
  // hasLinesOutside: for forward, pass hasLinesBelow; for backward, pass hasLinesAbove
  bool wouldCross(CursorPos cursor, const Lines& lines, int boundaryOffset,
                  bool hasLinesOutside) const {
    VimCore::WordBoundaryContext boundary;
    if (isForward) {
      boundary.rightColOffset = boundaryOffset;
      boundary.hasLinesBelow = hasLinesOutside;
    } else {
      boundary.leftColOffset = boundaryOffset;
      boundary.hasLinesAbove = hasLinesOutside;
    }
    CharRange range = VimCore::wordOperatorRange(
        cursor, lines, target, isBigWord, boundary);
    return range.begin == POSITION_OUTSIDE_BOUNDARY ||
           range.end == POSITION_OUTSIDE_BOUNDARY;
  }
};

// Get all word motion specs for testing
const std::vector<MotionSpec>& getAllWordMotions();

// =============================================================================
// Random Buffer Generation
// =============================================================================

// Test case structure for random buffer testing
struct RandomBufferTest {
  Lines lines;

  // Edit region bounds
  int editStartLine;
  int editStartCol;
  int editEndLine;
  int editEndCol;

  // Cursor position
  int cursorLine;
  int cursorCol;

  // Boundary positions (just outside the edit region)
  bool hasLeftBoundary;
  bool hasRightBoundary;
  CursorPos leftBoundaryPos;
  CursorPos rightBoundaryPos;

  // Context flags
  bool hasLinesAbove;
  bool hasLinesBelow;

  // For verification
  std::string prefix;  // Content before edit region (should be preserved)
  std::string suffix;  // Content after edit region (should be preserved)
};

// Generate a random buffer with boundary positions
RandomBufferTest generateRandomBuffer(int numLines);

// =============================================================================
// Boundary Crossing Verification
// =============================================================================

// Check if left boundary was crossed (prefix modified)
bool leftBoundaryCrossed(const RandomBufferTest& test, const Lines& result);

// Check if right boundary was crossed (suffix modified)
bool rightBoundaryCrossed(const RandomBufferTest& test, const Lines& result);

// =============================================================================
// Test Runner
// =============================================================================

// Run a single random test case for word motions
bool runRandomMotionTest(NeovimOracle& oracle, const MotionSpec& motion,
                         const RandomBufferTest& test, bool verbose = false);

// =============================================================================
// Text Object Test Infrastructure
// =============================================================================

struct TextObjectSpec {
  std::string cmd;   // e.g., "diw", "daw"
  bool isInner;      // true for iw/iW, false for aw/aW
  bool isBigWord;    // true for W variants

  // Predict if text object would cross boundaries using VimCore. Uses
  // offset-based boundary model: leftColOffset protects cols [0, offset) on
  // line 0; rightColOffset protects cols [lineLen-offset, lineLen) on the
  // last line.
  bool wouldCross(CursorPos cursor, const Lines& lines,
                  int leftColOffset, int rightColOffset,
                  bool hasLinesAbove, bool hasLinesBelow) const;
};

const std::vector<TextObjectSpec>& getAllTextObjects();

RandomBufferTest generateTextObjectBuffer(int numLines);

bool runTextObjectTest(NeovimOracle& oracle, const TextObjectSpec& spec,
                       const RandomBufferTest& test, bool verbose = false);
