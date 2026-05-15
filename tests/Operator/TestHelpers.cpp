// tests/Operator/TestHelpers.cpp
//
// Implementation of shared test infrastructure for operator boundary crossing tests.
// Uses VimEndpointUtils for CursorPos-based boundary checking.

#include "TestHelpers.h"

#include <algorithm>
#include <iostream>

#include "Types/CharRange.h"
#include "Utils/RandomGeneration.h"
#include "VimCore/VimEndpointUtils.h"

using namespace std;

// =============================================================================
// MotionSpec definitions
// =============================================================================

const vector<MotionSpec>& getAllWordMotions() {
  using VimCore::WordOperatorTarget;
  static vector<MotionSpec> motions = {
      {"de", true, WordOperatorTarget::DeleteToWordEnd, false},
      {"dw", true, WordOperatorTarget::DeleteToNextWord, false},
      {"db", false, WordOperatorTarget::DeleteBackToWordBegin, false},
      {"dge", false, WordOperatorTarget::DeleteBackToWordEnd, false},
      {"dE", true, WordOperatorTarget::DeleteToWordEnd, true},
      {"dW", true, WordOperatorTarget::DeleteToNextWord, true},
      {"dB", false, WordOperatorTarget::DeleteBackToWordBegin, true},
      {"dgE", false, WordOperatorTarget::DeleteBackToWordEnd, true},
  };
  return motions;
}

// =============================================================================
// Random Buffer Generation
// =============================================================================

RandomBufferTest generateRandomBuffer(int numLines) {
  RandomBufferTest test;

  // Build lines with mixed content using shared helper
  for (int i = 0; i < numLines; i++) {
    int len = RandomGen::range(8, 20);
    test.lines.push_back(randomProseLine(len));
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

  test.editStartCol = RandomGen::range(1, max(1, startLineLen - minEditLen));

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
    test.editEndCol = RandomGen::range(minEnd, maxEnd);
  } else {
    test.editEndCol = RandomGen::range(minEditLen - 1, endLineLen - 2);
  }

  // Set boundary positions (just outside the edit region)
  test.hasLeftBoundary = (test.editStartCol > 0);
  test.hasRightBoundary =
      (test.editEndCol + 1 < static_cast<int>(test.lines[test.editEndLine].size()));

  if (test.hasLeftBoundary) {
    test.leftBoundaryPos = CursorPos(test.editStartLine, test.editStartCol - 1);
  }
  if (test.hasRightBoundary) {
    test.rightBoundaryPos = CursorPos(test.editEndLine, test.editEndCol + 1);
  }

  // Random cursor position within edit region
  if (test.editStartLine == test.editEndLine) {
    test.cursorCol = RandomGen::range(test.editStartCol, test.editEndCol);
    test.cursorLine = test.editStartLine;
  } else {
    test.cursorLine = RandomGen::range(test.editStartLine, test.editEndLine);

    if (test.cursorLine == test.editStartLine) {
      int maxCol = test.lines[test.cursorLine].size() - 1;
      test.cursorCol = RandomGen::range(test.editStartCol, maxCol);
    } else if (test.cursorLine == test.editEndLine) {
      test.cursorCol = RandomGen::range(0, test.editEndCol);
    } else {
      int maxCol = max(0, static_cast<int>(test.lines[test.cursorLine].size()) - 1);
      test.cursorCol = RandomGen::range(0, maxCol);
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
  CursorPos cursor(test.cursorLine, test.cursorCol);
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

// =============================================================================
// TextObjectSpec definitions and helpers
// =============================================================================

bool TextObjectSpec::wouldCross(CursorPos cursor, const Lines& lines,
                                int leftColOffset, int rightColOffset,
                                bool hasLinesAbove, bool hasLinesBelow) const {
  VimCore::WordBoundaryContext boundary;
  boundary.leftColOffset = leftColOffset;
  boundary.rightColOffset = rightColOffset;
  boundary.hasLinesAbove = hasLinesAbove;
  boundary.hasLinesBelow = hasLinesBelow;
  boundary.clampOutside = false;
  CharRange range = VimCore::wordTextObjectRange(
      cursor, lines,
      isInner ? VimCore::WordTextObjectKind::Inner
              : VimCore::WordTextObjectKind::Around,
      isBigWord, boundary);
  return range.begin == POSITION_OUTSIDE_BOUNDARY ||
         range.end == POSITION_OUTSIDE_BOUNDARY;
}

const vector<TextObjectSpec>& getAllTextObjects() {
  static vector<TextObjectSpec> textObjects = {
      {"diw", true, false},
      {"daw", false, false},
      {"diW", true, true},
      {"daW", false, true},
  };
  return textObjects;
}

RandomBufferTest generateTextObjectBuffer(int numLines) {
  RandomBufferTest test;
  test.lines = randomLines(numLines, 10, 20);

  int editLine = RandomGen::range(0, numLines - 1);
  int lineLen2 = static_cast<int>(test.lines[editLine].size());

  int minEditLen = 4;
  if (lineLen2 < minEditLen + 2) {
    test.lines[editLine] += string(minEditLen + 2 - lineLen2, 'x');
    lineLen2 = static_cast<int>(test.lines[editLine].size());
  }

  int editStart = RandomGen::range(1, max(1, lineLen2 - minEditLen - 1));
  int editEnd = RandomGen::range(editStart + minEditLen - 1,
                                 min(lineLen2 - 2, editStart + 10));

  test.editStartLine = editLine;
  test.editStartCol = editStart;
  test.editEndLine = editLine;
  test.editEndCol = editEnd;

  test.cursorCol = RandomGen::range(editStart, editEnd);
  test.cursorLine = editLine;

  test.hasLeftBoundary = true;
  test.hasRightBoundary = true;
  test.leftBoundaryPos = CursorPos(editLine, editStart - 1);
  test.rightBoundaryPos = CursorPos(editLine, editEnd + 1);

  string& line = test.lines[editLine];
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

  test.hasLinesAbove = (editLine > 0);
  test.hasLinesBelow = (editLine < numLines - 1);

  return test;
}

// VimCore boundary functions expect effectiveLines (only the edit-region
// lines, with offsets protecting prefix/suffix columns), NOT the full buffer.
// Text-object tests use single-line edit regions, so the cursor and offsets
// are translated accordingly before calling wordTextObjectRange.
bool runTextObjectTest(NeovimOracle& oracle, const TextObjectSpec& spec,
                       const RandomBufferTest& test, bool verbose) {
  auto result = oracle.simulate(test.lines, test.cursorLine, test.cursorCol, spec.cmd);

  bool leftCrossed = test.hasLeftBoundary && leftBoundaryCrossed(test, result.lines);
  bool rightCrossed = test.hasRightBoundary && rightBoundaryCrossed(test, result.lines);

  Lines effectiveLines;
  for (int i = test.editStartLine; i <= test.editEndLine; i++) {
    effectiveLines.push_back(test.lines[i]);
  }
  int effectiveLastLine = static_cast<int>(effectiveLines.size()) - 1;

  CursorPos cursor(test.cursorLine - test.editStartLine, test.cursorCol);

  int leftColOffset = test.hasLeftBoundary ? test.editStartCol : 0;
  int rightColOffset = 0;
  if (test.hasRightBoundary) {
    int lineLen = static_cast<int>(effectiveLines[effectiveLastLine].size());
    rightColOffset = lineLen - test.editEndCol - 1;
  }

  bool predictCross = spec.wouldCross(cursor, effectiveLines, leftColOffset,
                                       rightColOffset, test.hasLinesAbove,
                                       test.hasLinesBelow);
  bool actualCross = leftCrossed || rightCrossed;
  bool success = !(actualCross && !predictCross);

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
    cerr << "Left offset: " << leftColOffset << ", Right offset: " << rightColOffset << endl;
    cerr << "Result:" << endl;
    for (size_t i = 0; i < result.lines.size(); i++) {
      cerr << "  [" << i << "]: \"" << result.lines[i] << "\"" << endl;
    }
    cerr << "Predicted: " << (predictCross ? "CROSS" : "SAFE")
         << ", Actual: " << (actualCross ? "CROSSED" : "SAFE") << endl;
  }

  return success;
}
