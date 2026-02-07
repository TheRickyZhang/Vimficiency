#pragma once

#include <array>
#include <vector>
#include <string>
#include "Editor/Position.h"
#include "Keyboard/KeyedSequence.h"
#include "Utils/Lines.h"

struct RepeatMotionResult {
  Position pos{-1, -1};
  int count = 0;  // count <= 1 means invalid (not worth emitting {count}{motion})

  bool valid() const { return count > 1; }
};

// Landing position categories - used as array index
enum class LandingType : size_t {
  WordBegin = 0,  // w, b
  WordEnd = 1,    // e, ge
  WORDBegin = 2,  // W, B
  WORDEnd = 3,    // E, gE
  Paragraph = 4,  // {, }
  Sentence = 5,   // (, )
  COUNT = 6
};

// Pairs a forward/backward motion with its landing type for count-searchable motions
struct CountableMotionPair {
  KeyedSequence forward;   // e.g., KeyedSequence::w, KeyedSequence::e
  KeyedSequence backward;  // e.g., KeyedSequence::b, KeyedSequence::ge
  LandingType type;
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


  // Debug
  size_t count(LandingType type) const { return get(type).size(); }
  const std::vector<Position>& getPositions(LandingType type) const { return get(type); }
};
