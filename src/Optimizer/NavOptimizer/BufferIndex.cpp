#include "BufferIndex.h"
#include "VimCore/CharMask.h"
#include "VimCore/VimCore.h"
#include <algorithm>
#include <assert.h>

using namespace VimCore;

BufferIndex::BufferIndex(const Lines& buffer) {
  if (buffer.empty()) return;

  bool prevLineWasEmpty = true;  // Treat sentinel as empty for paragraph detection

  for (int line = 0; line < static_cast<int>(buffer.size()); ++line) {
    const std::string& ln = buffer[line];
    bool lineEmpty = ln.empty() ||
      std::all_of(ln.begin(), ln.end(), [](char c) {
        return CharMask::isBlank(c);
      });
    // Paragraph boundary: empty line, or first non-empty after empty
    if (lineEmpty) {
      get(LandingType::Paragraph).emplace_back(line, 0);
    } else if (prevLineWasEmpty) {
      // First non-empty line after empty - also a paragraph boundary for {
      get(LandingType::Paragraph).emplace_back(line, 0);
    }
    prevLineWasEmpty = lineEmpty;

    // A truly empty line is itself a word and a WORD: w/W and b/B stop on it
    // (e/ge have no "end" there, so WordEnd/BigWordEnd are left out). A
    // whitespace-only line is not a word, so this is gated on ln.empty(), not
    // lineEmpty.
    if (ln.empty()) {
      get(LandingType::WordBegin).emplace_back(line, 0);
      get(LandingType::BigWordBegin).emplace_back(line, 0);
    }

    for (int col = 0; col < static_cast<int>(ln.size()); ++col) {
      char curr = ln[col];
      const bool atLineStart = col == 0;
      const bool atLineEnd = col + 1 == static_cast<int>(ln.size());

      CharMask currClass(curr);

      // Word/bigWord BEGIN: non-blank where previous was blank or different type
      if (atLineStart) {
        if (currClass.bigWord()) {
          get(LandingType::WordBegin).emplace_back(line, col);
          get(LandingType::BigWordBegin).emplace_back(line, col);
        }
      } else {
        CharMask prevClass(ln[col - 1]);
        if (beginsWordBroad(prevClass, currClass)) {
          get(LandingType::WordBegin).emplace_back(line, col);
        }
        if (beginsBigWordBroad(prevClass, currClass)) {
          get(LandingType::BigWordBegin).emplace_back(line, col);
        }
      }

      // Word/bigWord END: non-blank where next is blank or different type
      if (atLineEnd) {
        if (currClass.bigWord()) {
          get(LandingType::WordEnd).emplace_back(line, col);
          get(LandingType::BigWordEnd).emplace_back(line, col);
        }
      } else {
        CharMask nextClass(ln[col + 1]);
        if (endsWordBroad(currClass, nextClass)) {
          get(LandingType::WordEnd).emplace_back(line, col);
        }
        if (endsBigWordBroad(currClass, nextClass)) {
          get(LandingType::BigWordEnd).emplace_back(line, col);
        }
      }
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
