// tests/Utils/EditTestGenerators.cpp
//
// Implementation of Edit-specific test utilities.
// Core random generation is now in RandomBufferHelpers.h (header-only).

#include "EditTestGenerators.h"

#include "Utils/RandomGeneration.h"

using namespace std;

// =============================================================================
// Position Index Utilities
// =============================================================================

int toFlatIndex(int row, int col, const Lines& lines) {
  int idx = 0;
  for (int r = 0; r < row && r < static_cast<int>(lines.size()); r++) {
    idx += lines[r].empty() ? 1 : static_cast<int>(lines[r].size());
  }
  return idx + col;
}

Position fromFlatIndex(int flatIdx, const Lines& lines) {
  int remaining = flatIdx;
  for (int r = 0; r < static_cast<int>(lines.size()); r++) {
    int lineSize = lines[r].empty() ? 1 : static_cast<int>(lines[r].size());
    if (remaining < lineSize) {
      return Position(r, remaining);
    }
    remaining -= lineSize;
  }
  return Position(-1, -1);  // Invalid
}

// =============================================================================
// EmbeddedEditRegion Methods
// =============================================================================

EditBoundary EmbeddedEditRegion::makeBoundary() const {
  return EditBoundary(fullBuffer, {startLine, startCol}, {endLine, endCol});
}

Position EmbeddedEditRegion::toFullBufferPos(const Position& editPos) const {
  Position result = editPos;
  result.line += startLine;
  if (editPos.line == 0) {
    result.col += static_cast<int>(prefix.size());
  }
  return result;
}

string EmbeddedEditRegion::expectedAfterDeletion() const {
  return prefix + suffix;
}

// =============================================================================
// EmbeddedEditRegion Generators
// =============================================================================

EmbeddedEditRegion generateSingleLineEmbedded(
    int prefixLen,
    int contentLen,
    int suffixLen) {

  EmbeddedEditRegion result;

  result.prefix = randomWord(prefixLen);
  string content = randomWord(contentLen);
  result.suffix = randomWord(suffixLen);

  result.fullBuffer = {result.prefix + content + result.suffix};
  result.editRegion = {content};

  result.startLine = 0;
  result.startCol = static_cast<int>(result.prefix.size());
  result.endLine = 0;
  result.endCol = result.startCol + static_cast<int>(content.size()) - 1;

  return result;
}

EmbeddedEditRegion generateMultiLineEmbedded(
    int numLines,
    int minLineLen,
    int maxLineLen,
    int prefixLen,
    int suffixLen) {

  EmbeddedEditRegion result;

  // Generate edit region content
  result.editRegion = randomLines(numLines, minLineLen, maxLineLen);

  // Generate prefix and suffix
  result.prefix = randomWord(prefixLen);
  result.suffix = randomWord(suffixLen);

  // Build full buffer with prefix/suffix baked in
  result.fullBuffer.reserve(numLines);
  for (int i = 0; i < numLines; i++) {
    string line = result.editRegion[i];
    if (i == 0) {
      line = result.prefix + line;
    }
    if (i == numLines - 1) {
      line = line + result.suffix;
    }
    result.fullBuffer.push_back(line);
  }

  // Set boundary positions
  result.startLine = 0;
  result.startCol = static_cast<int>(result.prefix.size());
  result.endLine = numLines - 1;
  result.endCol = static_cast<int>(result.fullBuffer[result.endLine].size()) -
                  static_cast<int>(result.suffix.size()) - 1;

  return result;
}

EmbeddedEditRegion generateRandomSingleLineEmbedded() {
  int prefixLen = RandomGen::range(1, 2);
  int contentLen = RandomGen::range(2, 4);
  int suffixLen = RandomGen::range(1, 2);
  return generateSingleLineEmbedded(prefixLen, contentLen, suffixLen);
}

EmbeddedEditRegion generateRandomMultiLineEmbedded() {
  int numLines = RandomGen::range(2, 4);
  int minLineLen = 3;
  int maxLineLen = 5;
  int prefixLen = RandomGen::range(1, 3);
  int suffixLen = RandomGen::range(1, 3);
  return generateMultiLineEmbedded(numLines, minLineLen, maxLineLen, prefixLen, suffixLen);
}
