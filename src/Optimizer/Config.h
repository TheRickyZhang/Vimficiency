#pragma once
#include "Keyboard/KeyboardModel.h"
// #include "State.h"

struct Position;

// -----------------------------------------------------------------------------
// Key metadata and weights
// -----------------------------------------------------------------------------

struct KeyInfo {
  Hand hand       = Hand::None;
  Finger finger   = Finger::None;
  double base_cost = 0.0;

  KeyInfo() = default;
  KeyInfo(Hand h, Finger f, double cost) : hand(h), finger(f), base_cost(cost) {}
};

// todo tune these
struct ScoreWeights final {
  double w_key         =  1.0;   // base key cost
  double w_same_finger =  0;     // Pressing same finger
  double w_same_key    = -0.2;   // Counter to traditional typing, it is easier to process repeated key types.
  double w_alt_bonus   = -0.1;   // alternating hands
  double w_run_pen     =  0.0;   // penalty per step beyond RUN_THRESHOLD
  double w_roll_good   = -0.2;   // "good" rolls
  double w_roll_bad    =  0.2;   // "bad" rolls

  ScoreWeights() = default;

  ScoreWeights(double key, double same_finger = 0, double same_key = 0,
               double alt_hand_bonus = 0, double run_pen = 0, double roll_good = 0, double roll_bad = 0)
    : w_key(key), w_same_finger(same_finger), w_same_key(same_key),
              w_alt_bonus(alt_hand_bonus), w_run_pen(run_pen), w_roll_good(roll_good), w_roll_bad(roll_bad) {}
};

struct Config {
  std::array<KeyInfo, KEY_COUNT> keyInfo{};
  
  // See 
  ScoreWeights weights{};
};
