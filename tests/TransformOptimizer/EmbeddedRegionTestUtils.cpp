#include "TransformOptimizer/EmbeddedRegionTestUtils.h"

#include "Utils/RandomBufferHelpers.h"
#include "Utils/RandomGeneration.h"

using namespace std;

TransformBoundary EmbeddedEditRegion::makeBoundary() const {
  return TransformBoundary(fullBuffer, {startLine, startCol}, {endLine, endCol});
}

CursorPos EmbeddedEditRegion::toFullBufferPos(const CursorPos& editPos) const {
  CursorPos result = editPos;
  result.line += startLine;
  if (editPos.line == 0) {
    result.col += static_cast<int>(prefix.size());
  }
  return result;
}

string EmbeddedEditRegion::expectedAfterDeletion() const {
  return prefix + suffix;
}

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
  result.endCol = result.startCol + static_cast<int>(content.size());

  return result;
}

EmbeddedEditRegion generateMultiLineEmbedded(
    int numLines,
    int minLineLen,
    int maxLineLen,
    int prefixLen,
    int suffixLen) {

  EmbeddedEditRegion result;

  result.editRegion = randomLines(numLines, minLineLen, maxLineLen);

  result.prefix = randomWord(prefixLen);
  result.suffix = randomWord(suffixLen);

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

  result.startLine = 0;
  result.startCol = static_cast<int>(result.prefix.size());
  result.endLine = numLines - 1;
  result.endCol = static_cast<int>(result.fullBuffer[result.endLine].size()) -
                  static_cast<int>(result.suffix.size());

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
  return generateMultiLineEmbedded(
      numLines, minLineLen, maxLineLen, prefixLen, suffixLen);
}
