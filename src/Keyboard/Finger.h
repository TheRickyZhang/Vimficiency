#pragma once

#include <cstdint>
#include <cassert>

#include "Hand.h"

enum class Finger : int8_t {
#define X(name, str) name,
#include "XMacroFinger.inc"
#undef X
  None
};

inline constexpr int FINGER_COUNT = static_cast<int>(Finger::None);

enum class FingerPosition : uint8_t {
  Pinky = 0,
  Ring = 1,
  Middle = 2,
  Index = 3,
  Thumb = 4,
  None,
};

inline FingerPosition fingerToPosition(Finger f) {
  assert(f != Finger::None);
  return static_cast<FingerPosition>(static_cast<uint8_t>(f) % 5);
}

inline Hand fingerToHand(Finger f) {
  assert(f != Finger::None);
  return static_cast<uint8_t>(f) < 5 ? Hand::Left : Hand::Right;
}

inline bool sameHand(Finger a, Finger b) {
  return fingerToHand(a) == fingerToHand(b);
}
