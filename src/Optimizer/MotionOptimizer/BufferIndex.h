#pragma once

#include <array>
#include <vector>
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/LandingType.h"

struct RepeatMotionResult {
  CursorPos pos{-1, -1};
  int count = 0;  // count <= 1 means invalid (not worth emitting {count}{motion})

  bool valid() const { return count > 1; }
};

class BufferIndex {
  // Future optimization: could merge all vectors into one contiguous array
  // with offset indices per type, improving cache locality
  std::array<std::vector<CursorPos>, static_cast<size_t>(LandingType::COUNT)> positions_;

  const std::vector<CursorPos>& get(LandingType type) const {
    return positions_[static_cast<size_t>(type)];
  }
  std::vector<CursorPos>& get(LandingType type) {
    return positions_[static_cast<size_t>(type)];
  }

public:
  BufferIndex() = default;

  // Builds index with single forward scan through buffer
  // Contains positions that you could land on by applying motion, including the very start/end positions (even if they don't match the pattern)
  BufferIndex(const Lines& buffer);

  // Apply motion: count > 0 forward, count < 0 backward
  // Returns current if motion cannot complete
  CursorPos apply(LandingType type, CursorPos current, int count) const;

  // Returns [undershoot, overshoot] positions closest to goalPos, with counts from currPos.
  // Direction inferred from currPos vs goalPos. Invalid entries have count <= 1.
  std::array<RepeatMotionResult, 2> getTwoClosest(LandingType type, CursorPos currPos, CursorPos goalPos) const;

  // Returns positions near range [rangeBegin, rangeEnd): one undershoot (last before range),
  // all in-range positions, and one overshoot (first past range), each with count from currPos.
  // Direction inferred from currPos vs rangeBegin.
  std::vector<RepeatMotionResult> getClosestInRange(
      LandingType type, CursorPos currPos,
      CursorPos rangeBegin, CursorPos rangeEnd) const;

  // Debug
  size_t count(LandingType type) const { return get(type).size(); }
  const std::vector<CursorPos>& getPositions(LandingType type) const { return get(type); }
};

// Non-owning reference to a BufferIndex with a line offset for coordinate conversion.
// Bundles the two values that only make sense together: a BufferIndex built from a full
// buffer, and the line offset needed to convert local subset coordinates to/from it.
// Always valid — use std::optional<BufferIndexRef> when the index may be absent.
struct BufferIndexRef {
  const BufferIndex& index;
  int lineOffset;

  BufferIndexRef(const BufferIndex& idx, int offset)
    : index(idx), lineOffset(offset) {}
};
