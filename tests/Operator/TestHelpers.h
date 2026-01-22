// tests/Operator/TestHelpers.h
//
// Shared test infrastructure for operator boundary crossing tests.
// These helpers generate random buffers and verify boundary crossing behavior
// using VimCore functions which handle Position-based boundaries.

#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEndpointUtils.h"

#include <random>
#include <string>
#include <vector>

// =============================================================================
// MotionSpec - Motion specification with VimCore-based prediction
// =============================================================================

struct MotionSpec {
  std::string cmd;   // e.g., "de", "dw", "db"
  bool isForward;    // direction of motion
  EdgeType edgeType; // for word motions
  bool isBigWord;    // true for W/E/B variants
  bool skipCurrent;  // de/dE/db/dB need true; dw/dW/dge/dgE need false

  // Predict if motion would cross boundary using VimCore
  // boundaryOffset: for forward, protects suffix (last N cols of last line)
  //                 for backward, protects prefix (first N cols of line 0)
  // hasLinesOutside: for forward, pass hasLinesBelow; for backward, pass hasLinesAbove
  bool wouldCross(Position cursor, const Lines& lines, int boundaryOffset,
                  bool hasLinesOutside) const {
    Position result = VimCore::motionWordEndpoint(
        cursor, lines, isForward, edgeType, isBigWord, skipCurrent,
        boundaryOffset, hasLinesOutside);
    return result == POSITION_OUTSIDE_BOUNDARY;
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
  Position leftBoundaryPos;
  Position rightBoundaryPos;

  // Context flags
  bool hasLinesAbove;
  bool hasLinesBelow;

  // For verification
  std::string prefix;  // Content before edit region (should be preserved)
  std::string suffix;  // Content after edit region (should be preserved)
};

// Generate a random buffer with boundary positions
RandomBufferTest generateRandomBuffer(std::mt19937& rng, int numLines);

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
