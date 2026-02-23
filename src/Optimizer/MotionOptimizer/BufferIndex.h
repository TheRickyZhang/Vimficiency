#pragma once

#include <array>
#include <vector>
#include "VimTypes/Position.h"
#include "VimTypes/Lines.h"
#include "VimTypes/LandingType.h"

struct RepeatMotionResult {
  Position pos{-1, -1};
  int count = 0;  // count <= 1 means invalid (not worth emitting {count}{motion})

  bool valid() const { return count > 1; }
};

class BufferIndex {
  // Future optimization: could merge all vectors into one contiguous array
  // with offset indices per type, improving cache locality
  std::array<std::vector<Position>, static_cast<size_t>(LandingType::COUNT)> positions_;

  const std::vector<Position>& get(LandingType type) const {
    return positions_[static_cast<size_t>(type)];
  }
  std::vector<Position>& get(LandingType type) {
    return positions_[static_cast<size_t>(type)];
  }

public:
  // Builds index with single forward scan through buffer
  // Contains positions that you could land on by applying motion, including the very start/end positions (even if they don't match the pattern)
  BufferIndex(const Lines& buffer);

  // Apply motion: count > 0 forward, count < 0 backward
  // Returns current if motion cannot complete
  Position apply(LandingType type, Position current, int count) const;

  // Returns [undershoot, overshoot] positions closest to goalPos, with counts from currPos.
  // Direction inferred from currPos vs goalPos. Invalid entries have count <= 1.
  std::array<RepeatMotionResult, 2> getTwoClosest(LandingType type, Position currPos, Position goalPos) const;

  // Returns positions near range [rangeFirst, rangeEnd): one undershoot (last before range),
  // all in-range positions, and one overshoot (first past range), each with count from currPos.
  // Direction inferred from currPos vs rangeFirst.
  std::vector<RepeatMotionResult> getClosestInRange(
      LandingType type, Position currPos,
      Position rangeFirst, Position rangeEnd) const;

  // Debug
  size_t count(LandingType type) const { return get(type).size(); }
  const std::vector<Position>& getPositions(LandingType type) const { return get(type); }
};

// Non-owning reference to a BufferIndex with a line offset for coordinate conversion.
// Bundles the two values that only make sense together: a BufferIndex built from a full
// buffer, and the line offset needed to convert local subset coordinates to/from it.
struct BufferIndexRef {
  const BufferIndex* index = nullptr;
  int lineOffset = 0;

  explicit operator bool() const { return index != nullptr; }
};
