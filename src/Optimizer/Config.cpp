#include "Config.h"

#include <bits/stdc++.h>
using namespace std;

void fillQwerty(Config& cfg);
void fillColemakDh(Config& cfg);
void fillUniform(Config& cfg);

Config Config::qwerty() {
  Config c;
  fillQwerty(c);
  return c;
}

Config Config::colemakDh() {
  Config c;
  fillColemakDh(c);
  return c;
}
Config Config::uniform() {
  Config c;
  fillUniform(c);
  return c;
}



// ---------------------------------------------------------------------------
// Regular Qwerty
// Q W E R T   Y U I O P
//  A S D F G   H J K L ;
//   Z X C V B   N M , . /
//
//     2.6  2.2  2.0  2.0  2.2    2.2  2.0  2.0  2.2  2.6
//     2.2  2.0  1.7  1.7  2.0    2.0  1.7  1.7  2.0  2.2
//     2.2  1.7  1.3  1.3  1.6    1.6  1.3  1.3  1.7  2.2
//     1.3  1.2  1.0  1.0  1.5    1.5  1.0  1.0  1.2  1.3
//          2.0  1.7 (1.0)            (1.0) 1.7  2.0
//                            0.8 
//

void fillQwerty(Config &model) {
  std::array<KeyInfo, KEY_COUNT> &keyInfo = model.keyInfo;

  auto set_key = [&](Key k, Hand h, Finger f, double base_cost) {
    keyInfo[static_cast<size_t>(k)] = {h, f, base_cost};
  };

  // Left-hand letters
  set_key(Key::Key_Q, Hand::Left,  Finger::Lp,   2.4);
  set_key(Key::Key_W, Hand::Left,  Finger::Lr,   1.8);
  set_key(Key::Key_E, Hand::Left,  Finger::Lm,   1.4);
  set_key(Key::Key_R, Hand::Left,  Finger::Li,   1.4);
  set_key(Key::Key_T, Hand::Left,  Finger::Li,   1.8);

  set_key(Key::Key_A, Hand::Left,  Finger::Lp,   1.3);
  set_key(Key::Key_S, Hand::Left,  Finger::Lr,   1.2);
  set_key(Key::Key_D, Hand::Left,  Finger::Lm,   1.0);
  set_key(Key::Key_F, Hand::Left,  Finger::Li,   1.0);
  set_key(Key::Key_G, Hand::Left,  Finger::Li,   1.5);

  set_key(Key::Key_Z, Hand::Left,  Finger::Lp,   2.4);
  set_key(Key::Key_X, Hand::Left,  Finger::Lr,   1.8);
  set_key(Key::Key_C, Hand::Left,  Finger::Lm,   1.4);
  set_key(Key::Key_V, Hand::Left,  Finger::Li,   1.4);
  set_key(Key::Key_B, Hand::Left,  Finger::Li,   1.8);

  // Right-hand letters
  set_key(Key::Key_Y, Hand::Right, Finger::Ri,   1.8);
  set_key(Key::Key_U, Hand::Right, Finger::Ri,   1.4);
  set_key(Key::Key_I, Hand::Right, Finger::Rm,   1.4);
  set_key(Key::Key_O, Hand::Right, Finger::Rr,   1.8);
  set_key(Key::Key_P, Hand::Right, Finger::Rp,   2.4);

  set_key(Key::Key_H, Hand::Right, Finger::Ri,   1.5);
  set_key(Key::Key_J, Hand::Right, Finger::Ri,   1.0);
  set_key(Key::Key_K, Hand::Right, Finger::Rm,   1.0);
  set_key(Key::Key_L, Hand::Right, Finger::Rr,   1.2);
  set_key(Key::Key_Semicolon,  Hand::Right, Finger::Rp, 1.2);

  set_key(Key::Key_N, Hand::Right, Finger::Ri,   1.8);
  set_key(Key::Key_M, Hand::Right, Finger::Ri,   1.4);
  set_key(Key::Key_Comma,  Hand::Right, Finger::Rm, 1.4);
  set_key(Key::Key_Period, Hand::Right, Finger::Rr, 1.8);
  set_key(Key::Key_Slash,  Hand::Right, Finger::Rp, 2.4);

  set_key(Key::Key_1, Hand::Left,  Finger::Lp,   2.6);
  set_key(Key::Key_2, Hand::Left,  Finger::Lr,   2.2);
  set_key(Key::Key_3, Hand::Left,  Finger::Lm,   2.0);
  set_key(Key::Key_4, Hand::Left,  Finger::Li,   2.0);
  set_key(Key::Key_5, Hand::Left,  Finger::Li,   2.2);

  set_key(Key::Key_6, Hand::Right, Finger::Ri,   2.2);
  set_key(Key::Key_7, Hand::Right, Finger::Ri,   2.0);
  set_key(Key::Key_8, Hand::Right, Finger::Rm,   2.0);
  set_key(Key::Key_9, Hand::Right, Finger::Rr,   2.2);
  set_key(Key::Key_0, Hand::Right, Finger::Rp,   2.6);

  set_key(Key::Key_Grave, Hand::Left,  Finger::Lp,  3.0);
  set_key(Key::Key_Minus, Hand::Right, Finger::Rp,  3.0);
  set_key(Key::Key_Equal, Hand::Right, Finger::Rp,  3.0);

  // ---------------------------------------------------------------------------
  // MAIN PUNCTUATION KEYS
  // ---------------------------------------------------------------------------
  set_key(Key::Key_LBracket,  Hand::Right, Finger::Rp, 2.5);
  set_key(Key::Key_RBracket,  Hand::Right, Finger::Rp, 2.5);
  set_key(Key::Key_Backslash, Hand::Right, Finger::Rp, 3.0);
  set_key(Key::Key_Apostrophe, Hand::Right, Finger::Rp, 1.8);

  set_key(Key::Key_Esc,       Hand::Left,   Finger::Lp,   1.2);
  set_key(Key::Key_Tab,       Hand::Left,   Finger::Lp,   2.5);
  set_key(Key::Key_Enter,     Hand::Right,  Finger::Rp,   2.5);
  set_key(Key::Key_Backspace, Hand::Right,  Finger::Rp,   3.0);
  set_key(Key::Key_Space,     Hand::Right,   Finger::Rt, 0.8);
  set_key(Key::Key_Delete,    Hand::Right,  Finger::Ri,   3.0);

  set_key(Key::Key_Ctrl,  Hand::Left, Finger::Lp, 2.5);
  set_key(Key::Key_Shift, Hand::Left, Finger::Lp, 1.2);

  set_key(Key::Key_Home, Hand::Right, Finger::Ri, 3.0);
  set_key(Key::Key_End,  Hand::Right, Finger::Ri, 3.0);

  set_key(Key::Key_Left,  Hand::Right, Finger::Rr, 3.0);
  set_key(Key::Key_Down,  Hand::Right, Finger::Rm, 3.0);
  set_key(Key::Key_Right, Hand::Right, Finger::Ri, 3.0);
  set_key(Key::Key_Up,    Hand::Right, Finger::Rm, 3.0);
}


// ---------------------------------------------------------------------------
// Ortholinear Colemak DH:
// Q W F P B   J L U Y ;
// A R S T G   M N E I O
// Z X C D V   K H , . /
//
//       2.2  2.0  1.7  1.7  2.0    2.0  1.7  1.7  2.0  2.2
//       2.2  1.7  1.3  1.3  1.6    1.6  1.3  1.3  1.7  2.2 (2.5)
// (1.8) 1.3  1.2  1.0  1.0  1.5    1.5  1.0  1.0  1.2  1.3 (1.8)
// (1.2) 2.2  1.7  1.3  1.3  1.6    1.6  1.3  1.3  1.7  2.2 (1.2)
//            2.0  1.7 (1.0)            (1.0) 1.7  2.0
//                           1.2    1.2
//                      0.8  1.0    1.0  0.8
//
// ---------------------------------------------------------------------------
void fillColemakDh(Config &model) {
  auto &keyInfo = model.keyInfo;

  auto set_key = [&](Key k, Hand h, Finger f, double base_cost) {
    keyInfo[static_cast<size_t>(k)] = {h, f, base_cost};
  };

  // Left-hand letters
  set_key(Key::Key_Q, Hand::Left,  Finger::Lp,   2.2);
  set_key(Key::Key_W, Hand::Left,  Finger::Lr,   1.7);
  set_key(Key::Key_F, Hand::Left,  Finger::Lm,   1.3);
  set_key(Key::Key_P, Hand::Left,  Finger::Li,   1.3);
  set_key(Key::Key_B, Hand::Left,  Finger::Li,   1.6);

  set_key(Key::Key_A, Hand::Left,  Finger::Lp,   1.3);
  set_key(Key::Key_R, Hand::Left,  Finger::Lr,   1.2);
  set_key(Key::Key_S, Hand::Left,  Finger::Lm,   1.0);
  set_key(Key::Key_T, Hand::Left,  Finger::Li,   1.0);
  set_key(Key::Key_G, Hand::Left,  Finger::Li,   1.5);

  set_key(Key::Key_Z, Hand::Left,  Finger::Lp,   2.2);
  set_key(Key::Key_X, Hand::Left,  Finger::Lr,   1.7);
  set_key(Key::Key_C, Hand::Left,  Finger::Lm,   1.3);
  set_key(Key::Key_D, Hand::Left,  Finger::Li,   1.3);
  set_key(Key::Key_V, Hand::Left,  Finger::Li,   1.6);

  // Right-hand letters
  set_key(Key::Key_J, Hand::Right, Finger::Ri,   1.6);
  set_key(Key::Key_L, Hand::Right, Finger::Ri,   1.3);
  set_key(Key::Key_U, Hand::Right, Finger::Rm,   1.3);
  set_key(Key::Key_Y, Hand::Right, Finger::Rr,   1.7);
  set_key(Key::Key_Semicolon, Hand::Right, Finger::Rp,   2.2);

  set_key(Key::Key_M, Hand::Right, Finger::Ri,   1.5);
  set_key(Key::Key_N, Hand::Right, Finger::Ri,   1.0);
  set_key(Key::Key_E, Hand::Right, Finger::Rm,   1.0);
  set_key(Key::Key_I, Hand::Right, Finger::Rr,   1.2);
  set_key(Key::Key_O,  Hand::Right, Finger::Rp, 1.3);

  set_key(Key::Key_K, Hand::Right, Finger::Ri,   1.6);
  set_key(Key::Key_H, Hand::Right, Finger::Ri,   1.3);
  set_key(Key::Key_Comma,  Hand::Right, Finger::Rm, 1.3);
  set_key(Key::Key_Period, Hand::Right, Finger::Rr, 1.7);
  set_key(Key::Key_Slash,  Hand::Right, Finger::Rp, 2.2);

  set_key(Key::Key_1, Hand::Left,  Finger::Lp,   2.2);
  set_key(Key::Key_2, Hand::Left,  Finger::Lr,   2.0);
  set_key(Key::Key_3, Hand::Left,  Finger::Lm,   1.7);
  set_key(Key::Key_4, Hand::Left,  Finger::Li,   1.7);
  set_key(Key::Key_5, Hand::Left,  Finger::Li,   2.0);

  set_key(Key::Key_6, Hand::Right, Finger::Ri,   2.2);
  set_key(Key::Key_7, Hand::Right, Finger::Ri,   1.7);
  set_key(Key::Key_8, Hand::Right, Finger::Rm,   1.7);
  set_key(Key::Key_9, Hand::Right, Finger::Rr,   2.0);
  set_key(Key::Key_0, Hand::Right, Finger::Rp,   2.2);

  set_key(Key::Key_Grave, Hand::Left,  Finger::Li,  2.8);
  set_key(Key::Key_Minus, Hand::Right, Finger::Rp,  2.5);
  set_key(Key::Key_Equal, Hand::Left, Finger::Li,  2.5);

  // ---------------------------------------------------------------------------
  // MAIN PUNCTUATION KEYS
  // ---------------------------------------------------------------------------
  set_key(Key::Key_LBracket,  Hand::Right, Finger::Rp, 1.5);
  set_key(Key::Key_RBracket,  Hand::Right, Finger::Rp, 1.5);
  set_key(Key::Key_Backslash, Hand::Right, Finger::Rp, 3.0);
  set_key(Key::Key_Apostrophe, Hand::Right, Finger::Rp, 1.8);

  set_key(Key::Key_Esc,       Hand::Left,   Finger::Lp,   1.2);
  set_key(Key::Key_Tab,       Hand::Left,   Finger::Lp,   1.8);
  set_key(Key::Key_Enter,     Hand::Right,  Finger::Rp,   1.0);
  set_key(Key::Key_Backspace, Hand::Right,  Finger::Rp,   0.8);
  set_key(Key::Key_Space,     Hand::Right,   Finger::Lt, 0.8);
  set_key(Key::Key_Delete,    Hand::Right,  Finger::Ri,   2.5);

  set_key(Key::Key_Ctrl,  Hand::Left, Finger::Lp, 1.2);
  set_key(Key::Key_Shift, Hand::Left, Finger::Lp, 1.2);

  set_key(Key::Key_Home, Hand::Right, Finger::Ri, 3.0);
  set_key(Key::Key_End,  Hand::Right, Finger::Ri, 3.0);

  set_key(Key::Key_Left,  Hand::Right, Finger::Rr, 2.0);
  set_key(Key::Key_Down,  Hand::Right, Finger::Rm, 1.7);
  set_key(Key::Key_Right, Hand::Right, Finger::Ri, 1.7);
  set_key(Key::Key_Up,    Hand::Right, Finger::Rm, 2.0);
}

/*
* Equal weights for testing purposes
* 
* Everything 1.0, no complex score weights
*/

const vector<Key> modifierKeys = {
  Key::Key_Shift, Key::Key_Ctrl
};

void fillUniform(Config& cfg) {
  for(auto &ki : cfg.keyInfo) {
    ki.base_cost = 1.0;
  }
  for(Key k : modifierKeys) {
    cfg.keyInfo[static_cast<int>(k)].base_cost = 0.0;
  }
  cfg.weights = ScoreWeights();
}

