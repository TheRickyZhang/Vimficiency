#pragma once
#include "Keyboard/KeyboardModel.h"
#include "Utils/Debug.h"
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
  double w_key  =  1.0;   // base key cost
  double w_same_finger{}; // Pressing same finger
  double w_same_key{};    // It is actually easier to process repeated key types.
  double w_alt_bonus{};   // alternating hands
  double w_run_pen{};     // penalty per step beyond RUN_THRESHOLD
  double w_roll_good{};   // "good" rolls
  double w_roll_bad{};    // "bad" rolls

  ScoreWeights() = default;

  ScoreWeights(std::string setting) :
      w_same_finger(0),
      w_same_key(-0.2),
      w_alt_bonus(-0.1),
      w_run_pen(0.0),
      w_roll_good(-0.2),
      w_roll_bad(0.2)
  {
    debug("initialized with", setting);
  }

  ScoreWeights(double key, double same_finger, double same_key,
               double alt_hand_bonus, double run_pen, double roll_good,
               double roll_bad)
    :  w_same_finger(same_finger), w_same_key(same_key),
              w_alt_bonus(alt_hand_bonus), w_run_pen(run_pen),
              w_roll_good(roll_good), w_roll_bad(roll_bad) {}
};

// Use factory pattern
struct Config {
  std::array<KeyInfo, KEY_COUNT> keyInfo{};
  
  ScoreWeights weights{};

  static Config qwerty();
  static Config colemak_dh();
  static Config uniform();

private:
  Config() = default;
};
