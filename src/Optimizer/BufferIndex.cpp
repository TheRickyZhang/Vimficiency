#include "BufferIndex.h"
#include "VimCore/VimUtils.h"
#include "VimCore/VimMovementUtils.h"
#include <algorithm>
#include <assert.h>

using namespace VimUtils;

BufferIndex::BufferIndex(const std::vector<std::string>& buffer) {
  if (buffer.empty()) return;

  Position firstNonBlank{-1, -1};
  Position lastNonBlank{-1, -1};

  bool prevWasSentenceEnd = false;
  bool prevLineWasEmpty = true;  // Treat sentinel as empty for paragraph detection

  for (int line = 0; line < static_cast<int>(buffer.size()); ++line) {
    const std::string& ln = buffer[line];
    bool lineEmpty = ln.empty() ||
      std::all_of( ln.begin(), ln.end(), [](char c) { return isBlank(c); });
    // Paragraph boundary: empty line, or first non-empty after empty
    if (lineEmpty) {
      get(LandingType::Paragraph).emplace_back(line, 0);
    } else if (prevLineWasEmpty) {
      // First non-empty line after empty - also a paragraph boundary for {
      get(LandingType::Paragraph).emplace_back(line, 0);
    }
    prevLineWasEmpty = lineEmpty;

    if (ln.empty()) {
      prevWasSentenceEnd = false;
      continue;
    }

    for (int col = 0; col < static_cast<int>(ln.size()); ++col) {
      char curr = ln[col];
      char prev = (col > 0) ? ln[col - 1] : '\0';
      char next = (col + 1 < static_cast<int>(ln.size())) ? ln[col + 1] : '\0';

      // Track first/last non-blank for boundary sentinels
      if (!isBlank(curr)) {
        if (firstNonBlank.line == -1) {
          firstNonBlank = {line, col};
        }
        lastNonBlank = {line, col};
      }

      bool currIsWord = isSmallWordChar(curr);
      bool prevIsWord = isSmallWordChar(prev);
      bool currIsBigWord = isBigWordChar(curr);
      bool prevIsBigWord = isBigWordChar(prev);

      // Word/WORD BEGIN: non-blank where previous was blank or different type
      if (currIsWord && (col == 0 || isBlank(prev) || !prevIsWord)) {
        get(LandingType::WordBegin).emplace_back(line, col);
      }
      if (currIsBigWord && (col == 0 || !prevIsBigWord)) {
        get(LandingType::WORDBegin).emplace_back(line, col);
      }

      // Word/WORD END: non-blank where next is blank or different type
      bool nextIsWord = isSmallWordChar(next);
      bool nextIsBigWord = isBigWordChar(next);
      if (currIsWord && (next == '\0' || isBlank(next) || !nextIsWord)) {
        get(LandingType::WordEnd).emplace_back(line, col);
      }
      if (currIsBigWord && (next == '\0' || !nextIsBigWord)) {
        get(LandingType::WORDEnd).emplace_back(line, col);
      }

      // Sentence: first non-blank after sentence-ending punctuation + whitespace
      if (prevWasSentenceEnd && !isBlank(curr)) {
        get(LandingType::Sentence).emplace_back(line, col);
        prevWasSentenceEnd = false;
      }

      if (isSentenceEnd(curr) && (next == '\0' || isBlank(next))) {
        prevWasSentenceEnd = true;
      } else if (!isBlank(curr)) {
        prevWasSentenceEnd = false;
      }
    }

    // End of line with sentence-ending punct could lead to sentence start on next line
    if (!ln.empty() && isSentenceEnd(ln.back())) {
      prevWasSentenceEnd = true;
    }
  }

  // Add boundary sentinels to ensure getTwoClosest always has valid brackets
  if (firstNonBlank.line != -1) {
    for (size_t i = 0; i < static_cast<size_t>(LandingType::COUNT); ++i) {
      auto& vec = positions_[i];
      if (vec.empty() || vec.front() != firstNonBlank) {
        vec.insert(vec.begin(), firstNonBlank);
      }
      if (vec.back() != lastNonBlank) {
        vec.push_back(lastNonBlank);
      }
    }
  }
}

Position BufferIndex::apply(LandingType type, Position current, int count) const {
  if (count == 0) return current;

  const auto& positions = get(type);
  if (positions.empty()) return current;

  Position result = current;

  if (count > 0) {
    // Forward: find positions > current
    for (int i = 0; i < count; ++i) {
      auto it = std::upper_bound(positions.begin(), positions.end(), result);
      if (it == positions.end()) break;
      result = *it;
    }
  } else {
    // Backward: find positions < current
    for (int i = 0; i < -count; ++i) {
      auto it = std::lower_bound(positions.begin(), positions.end(), result);
      if (it == positions.begin()) break;
      result = *std::prev(it);
    }
  }

  return result;
}


// Some complex logic. Ensure it is correct!
std::array<RepeatMotionResult, 2>
BufferIndex::getTwoClosest(LandingType type, Position currPos, Position endPos) const {
  const auto& positions = get(type);


  auto calc2 = [&](auto begin, auto end, auto comp) -> std::array<RepeatMotionResult, 2> {
    auto onePastCurrIt = std::upper_bound(begin, end, currPos, comp);
    auto overshootIt   = std::lower_bound(begin, end, endPos,  comp);

    // By definition, all positions vectors should contain the very first/last positions possible (spamming w/e will get to boundaries)
    assert(overshootIt != end && overshootIt != begin);

    int dist = (int)std::distance(onePastCurrIt, overshootIt) + 1;
    assert(dist >= 1);

    return {
      RepeatMotionResult{*std::prev(overshootIt), dist - 1},
      RepeatMotionResult{*overshootIt,           dist},
    };
  };

  if(endPos > currPos) {
    return calc2(positions.begin(), positions.end(),
  [](const Position& a, const Position& b) { return a < b; });
  } else {
    // reverse view is descending, so comparator must flip
    return calc2(positions.rbegin(), positions.rend(),
                [](const Position& a, const Position& b) { return b < a; });
  }
}
