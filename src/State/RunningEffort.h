#pragma once

#include <bits/stdc++.h>

#include "Keyboard/KeyboardModel.h"
#include "Optimizer/Config.h"

// -----------------------------------------------------------------------------
// Incremental state (for a sequence of key presses)
// -----------------------------------------------------------------------------

class RunningEffort {
private:
  int    strokes           = 0;
  double sum_key_cost      = 0.0; // ∑ base_cost
  double sum_same_finger   = 0.0; // count/pen for same-finger bigrams
  double sum_same_key      = 0.0; // extra pen for same-key repeats
  double sum_alt_bonus     = 0.0; // count of alternations (to be rewarded)
  double sum_run_pen       = 0.0; // penalty for long same-hand runs
  double sum_roll_good     = 0.0; // count of "good" rolls
  double sum_roll_bad      = 0.0; // count of "bad" rolls

  // Short-term memory
  Key last_key         = Key::None;  // previous key index
  Finger last_finger       = Finger::None;
  Hand   last_hand         = Hand::None;

  Finger prev_finger       = Finger::None; // finger before last
  Hand   run_hand          = Hand::None;   // hand of current run
  int    run_len           = 0;               // length of current run

  // Append a key index [0..KEY_COUNT-1] and update all metrics.
  void appendSingle(Key key, const Config &model);

public:
  double getEffort(const Config &model) const;

  double append(KeySequence& keys, const Config& model);

  void reset();
};

double getEffort(const std::string &seq, const Config &cfg);

