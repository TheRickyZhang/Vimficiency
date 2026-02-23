#pragma once

#include <cstdint>

enum class Hand : int8_t {
#define X(name, str) name,
#include "XMacroHand.inc"
#undef X
  None
};

inline constexpr int HAND_COUNT = static_cast<int>(Hand::None);
