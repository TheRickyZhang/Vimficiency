#pragma once

enum class Key : int {
#define X(name, str) name,
#include "XMacroKey.inc"
#undef X
  None
};

inline constexpr int KEY_COUNT = static_cast<int>(Key::None);
