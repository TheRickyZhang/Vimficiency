#pragma once

#include <array>
#include <string>

#include "Finger.h"
#include "Hand.h"
#include "Key.h"
#include "Utils/Debug.h"

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
  double key_weight          = 1.0;
  double same_finger_weight{};
  double same_key_weight{};
  double alt_hand_weight{};
  double good_roll_weight{};
  double bad_roll_weight{};

  ScoreWeights() = default;

  ScoreWeights(std::string setting) :
      same_finger_weight(0),
      same_key_weight(-0.2),
      alt_hand_weight(-0.1),
      good_roll_weight(-0.2),
      bad_roll_weight(0.2)
  {
    debug("initialized with", setting);
  }

  ScoreWeights(double key, double same_finger, double same_key,
               double alt_hand, double good_roll, double bad_roll)
    : key_weight(key), same_finger_weight(same_finger),
      same_key_weight(same_key), alt_hand_weight(alt_hand),
      good_roll_weight(good_roll), bad_roll_weight(bad_roll) {}
};

// Use factory pattern
struct Config {
  std::array<KeyInfo, KEY_COUNT> keyInfo{};
  

  ScoreWeights weights{};

  static Config qwerty();
  static Config colemakDh();
  static Config uniform();

private:
  Config() = default;
};
