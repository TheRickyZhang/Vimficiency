// tests/Boundary/BoundaryTestHelpers.h
//
// Shared test infrastructure for boundary crossing tests.
// These helpers generate random buffers and verify boundary crossing behavior
// using VimEndpointUtils functions which handle Position-based boundaries.

#pragma once

#include "Editor/Position.h"
#include "Editor/Range.h"
#include "Utils/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEndpointUtils.h"

#include <functional>
#include <random>
#include <string>
#include <vector>

// =============================================================================
// MotionSpec - Motion specification with VimEndpointUtils-based prediction
// =============================================================================

struct MotionSpec {
  std::string cmd;   // e.g., "de", "dw", "db"
  bool isForward;    // direction of motion
  EdgeType edgeType; // for word motions
  bool isBigWord;    // true for W/E/B variants

  // Predict if motion would cross boundary using VimEndpointUtils
  bool wouldCross(Position cursor, const Lines& lines, Position boundary) const {
    Position result = VimEndpointUtils::motionWordEndpoint(
        cursor, lines, isForward, edgeType, isBigWord, false, boundary);
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
