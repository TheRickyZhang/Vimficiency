#pragma once

#include <array>
#include <vector>
#include "Types/Pos.h"
#include "Types/Lines.h"
#include "Types/LandingType.h"

struct RepeatMotionResult {
  Pos pos{};
  int count{}; // Only use count <= 1 -> invalid

  RepeatMotionResult() = default;
  RepeatMotionResult(Pos pos, int count) : pos(pos), count(count) {
    // Generally no point in in having count <= 1
    assert(count > 1);
  }
  bool valid() const { return count > 1; }
};

class BufferIndex {
  // Future optimization: could merge all vectors into one contiguous array
  // with offset indices per type, improving cache locality
  std::array<std::vector<Pos>, static_cast<size_t>(LandingType::COUNT)> positions_;

  const std::vector<Pos>& get(LandingType type) const {
    return positions_[static_cast<size_t>(type)];
  }
  std::vector<Pos>& get(LandingType type) {
    return positions_[static_cast<size_t>(type)];
  }

public:
  BufferIndex() = default;

  // Builds index with single forward scan through buffer
  // Contains positions that you could land on by applying motion, including the very start/end positions (even if they don't match the pattern)
  BufferIndex(const Lines& buffer);

  // Apply motion: count > 0 forward, count < 0 backward
  // Returns current if motion cannot complete
  Pos apply(LandingType type, Pos current, int count) const;

  // Returns all landings in inclusive [rangeFront, rangeBack], plus conditional near-misses.
  // Near-miss before rangeFront: only if no landing at exactly rangeFront.
  // Near-miss after rangeBack: only if no landing at exactly rangeBack.
  // If currPos is not strictly on the requested side of the range, returns {}.
  template<bool Forward>
  std::vector<RepeatMotionResult> getClosestInRange(
      LandingType type, Pos currPos,
      Pos rangeFront, Pos rangeBack) const;

  // Debug
  size_t count(LandingType type) const { return get(type).size(); }
  const std::vector<Pos>& getPositions(LandingType type) const { return get(type); }
};
