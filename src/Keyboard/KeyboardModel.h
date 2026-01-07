#pragma once

#include <cassert>
#include <cstdint>
#include <ostream>
#include <span>
#include <string.h>
#include <vector>

#include "XMacroKeyDefinitions.h"

static constexpr int KEY_COUNT = 61;
static constexpr int FINGER_COUNT = 10;
static constexpr int HAND_COUNT = 2; // Unless you're an amputee or an alien; not that those are correlated

static constexpr int RUN_THRESHOLD = 4; // More consecutive on same hand -> penalty


#define ENUM_VALUE(name, str) name,
enum class Key : int {
    VIMFICIENCY_KEYS(ENUM_VALUE)
    None
};


enum class Hand : int8_t {
    VIMFICIENCY_HANDS(ENUM_VALUE)
    None
};

enum class Finger : int8_t {
    VIMFICIENCY_FINGERS(ENUM_VALUE)
  None
};
#undef ENUM_VALUE

enum class FingerPosition : uint8_t {
  Pinky = 0, Ring = 1, Middle = 2, Index = 3, Thumb = 4,
  None,
};

// Represents physical key presses for effort calculation.
// Used by RunningEffort to compute typing cost based on hand/finger patterns.
class PhysicalKeys {
  std::vector<Key> keys;
public:
  PhysicalKeys() = default;
  PhysicalKeys(std::initializer_list<Key> init) : keys(init) {}

  size_t size() const { return keys.size(); }
  bool empty() const { return keys.empty(); }
  auto begin() const { return keys.begin(); }
  auto end() const { return keys.end(); }

  std::span<const Key> view() const { return keys; }
  void push_back(const Key& k) {
    keys.push_back(k);
  }
  PhysicalKeys& append(const PhysicalKeys& ks, size_t cnt = 1);

  PhysicalKeys& operator+=(const PhysicalKeys& other){
    return append(other);
  }
  bool operator==(const PhysicalKeys& other) const {
    return keys == other.keys;
  }
  bool operator!=(const PhysicalKeys& other) const {
    return !(*this == other);
  }
};

std::ostream& operator<<(std::ostream& os, const PhysicalKeys& ks);

static_assert(KEY_COUNT == static_cast<uint8_t>(Key::None), "key counts do not match");
static_assert(FINGER_COUNT == static_cast<uint8_t>(Finger::None), "finger counts do not match");
static_assert(HAND_COUNT == static_cast<uint8_t>(Hand::None), "hand counts do not match");
