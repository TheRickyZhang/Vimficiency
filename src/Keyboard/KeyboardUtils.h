#pragma once

#include <cassert>
#include <cstdint>

#include "KeyboardModel.h"

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
