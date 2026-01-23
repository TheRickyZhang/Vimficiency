// tests/Operator/TestHelpers.cpp
//
// Implementation of shared test infrastructure for operator boundary crossing tests.
// Uses VimEndpointUtils for Position-based boundary checking.

#include "TestHelpers.h"

#include <algorithm>
#include <iostream>

using namespace std;

// =============================================================================
// MotionSpec definitions
// =============================================================================

const vector<MotionSpec>& getAllWordMotions() {
  // cmd, isForward, edgeType, isBigWord, skipCurrent
  static vector<MotionSpec> motions = {
      // word motions (small)
      {"de", true, EdgeType::WordEdge, false, true},
      {"dw", true, EdgeType::GapEdge, false, false},
      {"db", false, EdgeType::WordEdge, false, true},
      {"dge", false, EdgeType::NextEdge, false, false},

      // WORD motions
      {"dE", true, EdgeType::WordEdge, true, true},
      {"dW", true, EdgeType::GapEdge, true, false},
      {"dB", false, EdgeType::WordEdge, true, true},
      {"dgE", false, EdgeType::NextEdge, true, false},
  };
  return motions;
}

// =============================================================================
// Random Buffer Generation
// =============================================================================

RandomBufferTest generateRandomBuffer(mt19937& rng, int numLines) {
  RandomBufferTest test;

  uniform_int_distribution<int> lineLen(8, 20);

  // Build lines with mixed content using shared helper
  for (int i = 0; i < numLines; i++) {
    int len = lineLen(rng);
    test.lines.push_back(randomMixedLine(rng, len));
  }

  // === IMPORTANT: effectiveLines Constraint ===
  // For multi-line buffers, the edit region MUST span the full buffer so that
  // test.lines == effectiveLines. This allows passing test.lines directly to
  // VimCore boundary functions in runRandomMotionTest.
  //
  // If partial multi-line edit regions are needed (e.g., TextObjects.cpp tests),
  // the test runner must construct effectiveLines from the edit region and translate
  // cursor coordinates. See TextObjects.cpp for an example.
  if (numLines > 1) {
    test.editStartLine = 0;
    test.editEndLine = numLines - 1;
  } else {
    test.editStartLine = 0;
    test.editEndLine = 0;
  }

  // Pick column bounds within first/last edit lines
  int startLineLen = test.lines[test.editStartLine].size();
  int endLineLen = test.lines[test.editEndLine].size();

  // Ensure minimum edit region size
  int minEditLen = 3;
  if (startLineLen < minEditLen + 2) {
    test.lines[test.editStartLine] += string(minEditLen + 2 - startLineLen, 'x');
    startLineLen = test.lines[test.editStartLine].size();
  }
  if (endLineLen < minEditLen + 2) {
    test.lines[test.editEndLine] += string(minEditLen + 2 - endLineLen, 'x');
    endLineLen = test.lines[test.editEndLine].size();
  }

  uniform_int_distribution<int> startColDist(1, max(1, startLineLen - minEditLen));
  test.editStartCol = startColDist(rng);

  if (test.editStartLine == test.editEndLine) {
    // Ensure valid range: end must be >= start + minEditLen - 1 AND < endLineLen - 1
    int minEnd = test.editStartCol + minEditLen - 1;
    int maxEnd = endLineLen - 2;
    if (minEnd > maxEnd) {
      // Extend line to accommodate
      test.lines[test.editEndLine] += string(minEnd - maxEnd + 1, 'x');
      endLineLen = test.lines[test.editEndLine].size();
      maxEnd = endLineLen - 2;
    }
    uniform_int_distribution<int> endColDist(minEnd, maxEnd);
    test.editEndCol = endColDist(rng);
  } else {
    uniform_int_distribution<int> endColDist(minEditLen - 1, endLineLen - 2);
    test.editEndCol = endColDist(rng);
  }

  // Set boundary positions (just outside the edit region)
  test.hasLeftBoundary = (test.editStartCol > 0);
  test.hasRightBoundary =
      (test.editEndCol + 1 < static_cast<int>(test.lines[test.editEndLine].size()));

  if (test.hasLeftBoundary) {
    test.leftBoundaryPos = Position(test.editStartLine, test.editStartCol - 1);
  }
  if (test.hasRightBoundary) {
    test.rightBoundaryPos = Position(test.editEndLine, test.editEndCol + 1);
  }

  // Random cursor position within edit region
  if (test.editStartLine == test.editEndLine) {
    uniform_int_distribution<int> cursorColDist(test.editStartCol, test.editEndCol);
    test.cursorCol = cursorColDist(rng);
    test.cursorLine = test.editStartLine;
  } else {
    uniform_int_distribution<int> cursorLineDist(test.editStartLine, test.editEndLine);
    test.cursorLine = cursorLineDist(rng);

    if (test.cursorLine == test.editStartLine) {
      int maxCol = test.lines[test.cursorLine].size() - 1;
      uniform_int_distribution<int> colDist(test.editStartCol, maxCol);
      test.cursorCol = colDist(rng);
    } else if (test.cursorLine == test.editEndLine) {
      uniform_int_distribution<int> colDist(0, test.editEndCol);
      test.cursorCol = colDist(rng);
    } else {
      int maxCol = max(0, static_cast<int>(test.lines[test.cursorLine].size()) - 1);
      uniform_int_distribution<int> colDist(0, maxCol);
      test.cursorCol = colDist(rng);
    }
  }

  // Context flags
  test.hasLinesAbove = (test.editStartLine > 0);
  test.hasLinesBelow = (test.editEndLine < numLines - 1);

  // Build prefix/suffix for verification
  string prefix;
  for (int i = 0; i < test.editStartLine; i++) {
    prefix += test.lines[i] + '\n';
  }
  prefix += test.lines[test.editStartLine].substr(0, test.editStartCol);
  test.prefix = prefix;

  string suffix = test.lines[test.editEndLine].substr(test.editEndCol + 1);
  for (int i = test.editEndLine + 1; i < numLines; i++) {
    suffix += '\n';
    suffix += test.lines[i];
  }
  test.suffix = suffix;

  return test;
}

// =============================================================================
// Boundary Crossing Verification
// =============================================================================

// Flatten Lines to string for comparison
static string flattenToString(const Lines& lines) {
  string result;
  for (size_t i = 0; i < lines.size(); i++) {
    if (i > 0) result += '\n';
    result += lines[i];
  }
  return result;
}

bool leftBoundaryCrossed(const RandomBufferTest& test, const Lines& result) {
  if (!test.hasLeftBoundary) return false;

  string resultStr = flattenToString(result);

  // Prefix should still be at the start
  if (resultStr.size() < test.prefix.size()) return true;
  return resultStr.substr(0, test.prefix.size()) != test.prefix;
}

bool rightBoundaryCrossed(const RandomBufferTest& test, const Lines& result) {
  if (!test.hasRightBoundary) return false;

  string resultStr = flattenToString(result);

  // Suffix should still be at the end
  if (resultStr.size() < test.suffix.size()) return true;
  return resultStr.substr(resultStr.size() - test.suffix.size()) != test.suffix;
}

// =============================================================================
// Test Runner
// =============================================================================

bool runRandomMotionTest(NeovimOracle& oracle, const MotionSpec& motion,
                         const RandomBufferTest& test, bool verbose) {
  // Execute command in Neovim
  auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, motion.cmd);

  // Check if boundaries were actually crossed
  bool leftCrossed = test.hasLeftBoundary && leftBoundaryCrossed(test, result.lines);
  bool rightCrossed = test.hasRightBoundary && rightBoundaryCrossed(test, result.lines);

  // Predict using VimEndpointUtils
  Position cursor(test.cursorLine, test.cursorCol);
  bool hasBoundary = motion.isForward ? test.hasRightBoundary : test.hasLeftBoundary;

  bool predictCross = false;
  if (hasBoundary) {
    // Compute boundary offset based on direction
    // Forward: suffix chars on last edit line = chars after editEndCol
    // Backward: prefix chars on first edit line = editStartCol
    int boundaryOffset;
    bool hasLinesOutside;
    if (motion.isForward) {
      int endLineLen = static_cast<int>(test.lines[test.editEndLine].size());
      boundaryOffset = endLineLen - test.editEndCol - 1;
      hasLinesOutside = test.hasLinesBelow;
    } else {
      boundaryOffset = test.editStartCol;
      hasLinesOutside = test.hasLinesAbove;
    }
    predictCross = motion.wouldCross(cursor, test.lines, boundaryOffset, hasLinesOutside);
  }

  // Check the relevant boundary based on direction
  bool actualCross = motion.isForward ? rightCrossed : leftCrossed;

  // Failure if: actual crossed but we predicted safe
  bool success = !(actualCross && !predictCross);

  if (verbose && !success) {
    cerr << "\n=== MOTION TEST FAILURE ===" << endl;
    cerr << "Command: " << motion.cmd << endl;
    cerr << "Input:" << endl;
    for (size_t i = 0; i < test.lines.size(); i++) {
      cerr << "  [" << i << "]: \"" << test.lines[i] << "\"" << endl;
    }
    cerr << "Cursor: (" << test.cursorLine << ", " << test.cursorCol << ")" << endl;
    cerr << "Edit region: (" << test.editStartLine << "," << test.editStartCol << ") to ("
         << test.editEndLine << "," << test.editEndCol << ")" << endl;
    if (motion.isForward && test.hasRightBoundary) {
      cerr << "Right boundary: (" << test.rightBoundaryPos.line << ","
           << test.rightBoundaryPos.col << ")" << endl;
    }
    if (!motion.isForward && test.hasLeftBoundary) {
      cerr << "Left boundary: (" << test.leftBoundaryPos.line << ","
           << test.leftBoundaryPos.col << ")" << endl;
    }
    cerr << "Result:" << endl;
    for (size_t i = 0; i < result.lines.size(); i++) {
      cerr << "  [" << i << "]: \"" << result.lines[i] << "\"" << endl;
    }
    cerr << "Predicted: " << (predictCross ? "CROSS" : "SAFE")
         << ", Actual: " << (actualCross ? "CROSSED" : "SAFE") << endl;
  }

  return success;
}
