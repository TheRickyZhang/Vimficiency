#pragma once
#include <bits/stdc++.h>
#include "KeyboardModel.h"

// -----------------------------------------------------------------------------
// Key metadata and weights
// -----------------------------------------------------------------------------

struct KeyInfo {
  Hand hand       = Hand::None;
  Finger finger   = Finger::None;
  double baseCost = 0.0;

  KeyInfo() = default;
  KeyInfo(Hand h, Finger f, double cost) : hand(h), finger(f), baseCost(cost) {}
};

// todo tune these
struct ScoreWeights final {
  double w_key         = 1.0;   // base key cost
  double w_same_finger = 10.0;  // penalty per same-finger bigram
  double w_same_key    = 15.0;  // extra penalty per same-key repeat
  double w_alt_bonus   = 1.0;   // reward for alternating hands
  double w_run_pen     = 0.5;   // penalty per step beyond RUN_THRESHOLD
  double w_roll_good   = 0.5;   // reward for "good" rolls
  double w_roll_bad    = 1.0;   // penalty for "bad" rolls
};

struct EffortModel {
  std::array<KeyInfo, KEY_COUNT> keyInfo{};
  ScoreWeights weights{};
};
