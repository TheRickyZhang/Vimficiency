#include "BufferIndex.h"
#include "VimCore/VimCore.h"
#include <algorithm>
#include <assert.h>

using namespace VimCore;

BufferIndex::BufferIndex(const Lines& buffer) {
  if (buffer.empty()) return;

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

      bool currIsWord = isSmallWordChar(curr);
      bool prevIsWord = isSmallWordChar(prev);
      bool currIsBigWord = isBigWordChar(curr);
      bool prevIsBigWord = isBigWordChar(prev);
      bool currIsNonBlank = !isBlank(curr);
      bool prevIsBlank = col == 0 || isBlank(prev);

      // Word/WORD BEGIN: non-blank where previous was blank or different type
      if (currIsNonBlank && (prevIsBlank || currIsWord != prevIsWord)) {
        get(LandingType::WordBegin).emplace_back(line, col);
      }
      if (currIsBigWord && (col == 0 || !prevIsBigWord)) {
        get(LandingType::WORDBegin).emplace_back(line, col);
      }

      // Word/WORD END: non-blank where next is blank or different type
      bool nextIsWord = isSmallWordChar(next);
      bool nextIsBigWord = isBigWordChar(next);
      if (currIsNonBlank && (next == '\0' || isBlank(next) || currIsWord != nextIsWord)) {
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

}

Pos BufferIndex::apply(LandingType type, Pos current, int count) const {
  if (count == 0) return current;

  const auto& positions = get(type);
  if (positions.empty()) return current;

  Pos result = current;

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


/*
* Preconditions: currPos not in [rangeFirst, rangeLast], direction matches Forward
* Returns: list of valid sorted RepeatMovementResults by position containing:
*   pre-first if no matches at first and ahead of start 
*   all matches inside range
*   post-last if no matches at last
*/
template<bool Forward>
std::vector<RepeatMovementResult>
BufferIndex::getClosestInRange(LandingType type, Pos currPos,
                               Pos rangeFirst, Pos rangeLast) const {
  return getClosestInRange<Forward>(
      type, currPos, rangeFirst, rangeLast, [](Pos) { return true; });
}

template<bool Forward>
std::vector<RepeatMovementResult>
BufferIndex::getClosestInRange(
    LandingType type, Pos currPos, Pos rangeFirst, Pos rangeLast,
    const std::function<bool(Pos)>& isAllowedEndpoint) const {
  const auto& positions = get(type);
  if (positions.empty()) return {};

  if constexpr (Forward) {
    if (!(currPos < rangeFirst)) return {};
  } else {
    if (!(currPos > rangeLast)) return {};
  }

  std::vector<RepeatMovementResult> results;
  auto pushIfAllowed = [&](Pos pos, int cnt) {
    if (cnt > 1 && isAllowedEndpoint(pos)) {
      results.push_back(RepeatMovementResult(pos, cnt));
    }
  };

  // [frontIt, pastBackIt) = landings in inclusive [rangeFirst, rangeLast]
  auto frontIt    = std::lower_bound(positions.begin(), positions.end(), rangeFirst);
  auto pastBackIt = std::upper_bound(positions.begin(), positions.end(), rangeLast);

  bool hasLandingAtFront = (frontIt != pastBackIt && *frontIt == rangeFirst);
  bool hasLandingAtBack  = (frontIt != pastBackIt && *std::prev(pastBackIt) == rangeLast);

  if constexpr (Forward) {
    auto onePastCurrIt = std::upper_bound(positions.begin(), positions.end(), currPos);

    // Near-miss before rangeFirst: only if no landing at exactly rangeFirst
    if (!hasLandingAtFront && frontIt != positions.begin()) {
      auto nearMissIt = std::prev(frontIt);
      int cnt = static_cast<int>(std::distance(onePastCurrIt, nearMissIt)) + 1;
      pushIfAllowed(*nearMissIt, cnt);
    }

    // All in-range landings
    for (auto it = frontIt; it != pastBackIt; ++it) {
      int cnt = static_cast<int>(std::distance(onePastCurrIt, it)) + 1;
      pushIfAllowed(*it, cnt);
    }

    // Near-miss after rangeLast: only if no landing at exactly rangeLast
    if (!hasLandingAtBack && pastBackIt != positions.end()) {
      int cnt = static_cast<int>(std::distance(onePastCurrIt, pastBackIt)) + 1;
      pushIfAllowed(*pastBackIt, cnt);
    }
  } else {
    auto currLowerIt = std::lower_bound(positions.begin(), positions.end(), currPos);

    // Near-miss after rangeLast: only if no landing at exactly rangeLast
    if (!hasLandingAtBack && pastBackIt != positions.end()) {
      int cnt = static_cast<int>(std::distance(pastBackIt, currLowerIt));
      pushIfAllowed(*pastBackIt, cnt);
    }

    // All in-range landings (reverse order for natural backward ordering)
    for (auto it = pastBackIt; it != frontIt; ) {
      --it;
      int cnt = static_cast<int>(std::distance(it, currLowerIt));
      pushIfAllowed(*it, cnt);
    }

    // Near-miss before rangeFirst: only if no landing at exactly rangeFirst
    if (!hasLandingAtFront && frontIt != positions.begin()) {
      auto nearMissIt = std::prev(frontIt);
      int cnt = static_cast<int>(std::distance(nearMissIt, currLowerIt));
      pushIfAllowed(*nearMissIt, cnt);
    }
  }

  return results;
}

template std::vector<RepeatMovementResult>
BufferIndex::getClosestInRange<true>(LandingType, Pos, Pos, Pos) const;

template std::vector<RepeatMovementResult>
BufferIndex::getClosestInRange<false>(LandingType, Pos, Pos, Pos) const;

template std::vector<RepeatMovementResult>
BufferIndex::getClosestInRange<true>(
    LandingType, Pos, Pos, Pos, const std::function<bool(Pos)>&) const;

template std::vector<RepeatMovementResult>
BufferIndex::getClosestInRange<false>(
    LandingType, Pos, Pos, Pos, const std::function<bool(Pos)>&) const;
