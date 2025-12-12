#pragma once
#include <bits/stdc++.h>

static constexpr int KEY_COUNT = 61;
static constexpr int FINGER_COUNT = 10;

static constexpr int RUN_THRESHOLD = 3; // More consecutive on same hand -> penalty

enum class Hand : uint8_t {
  Left, Right,
  None,
};

enum class Finger : uint8_t {
  Lp, Lr, Lm, Li, Lt,
  Ri, Rm, Rr, Rp, Rt,
  None,
};

enum class FingerPosition : uint8_t {
  Pinky = 0, Ring = 1, Middle = 2, Index = 3, Thumb = 4,
  None,
};

enum class Key : uint8_t {
  // 0..25
  Key_A, Key_B, Key_C, Key_D, Key_E, Key_F, Key_G, Key_H, Key_I,
  Key_J, Key_K, Key_L, Key_M, Key_N, Key_O, Key_P, Key_Q, Key_R,
  Key_S, Key_T, Key_U, Key_V, Key_W, Key_X, Key_Y, Key_Z,

  // 26..35
  Key_0, Key_1, Key_2, Key_3, Key_4, Key_5, Key_6, Key_7, Key_8, Key_9,

  // 36..46
  Key_Grave, Key_Minus, Key_Equal, Key_LBracket, Key_RBracket, Key_Backslash,
  Key_Semicolon, Key_Apostrophe, Key_Comma, Key_Period, Key_Slash,

  // 47..56
  Key_Esc, Key_Tab, Key_Enter, Key_Backspace, Key_Space, Key_Delete,
  Key_Ctrl, Key_Shift,
  Key_Home, Key_End,

  // 57..60
  Key_Up, Key_Down, Key_Left, Key_Right,

  None  // sentinel; must stay last
};

static_assert(KEY_COUNT == static_cast<uint8_t>(Key::None), "key counts do not match");
static_assert(FINGER_COUNT == static_cast<uint8_t>(Finger::None), "finger counts do not match");
