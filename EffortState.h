#pragma once

#include <bits/stdc++.h>
using namespace std;

#include "KeyboardModel.h"
#include "KeyboardUtils.h"
#include "Config.h"


// -----------------------------------------------------------------------------
// Incremental state (for a sequence of key presses)
// -----------------------------------------------------------------------------

struct EffortState {
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

  double get_cost(const Config &model) const {
    const auto &w = model.weights;

    double s = 0.0;
    s += w.w_key         * sum_key_cost;
    s += w.w_same_finger * sum_same_finger;
    s += w.w_same_key    * sum_same_key;
    s -= w.w_alt_bonus   * sum_alt_bonus;   // alternation makes it cheaper
    s += w.w_run_pen     * sum_run_pen;
    s -= w.w_roll_good   * sum_roll_good;   // good rolls are cheaper
    s += w.w_roll_bad    * sum_roll_bad;

    return s;
  }


  // Returns difference in cost after appending keys
  int append(vector<Key>& keys, const Config& model) {
    for(Key k : keys) {
      append_single(k, model);
    }
    return get_cost(model);
  }

  // Append a key index [0..KEY_COUNT-1] and update all metrics.
  void append_single(Key key, const Config &model) {
    const KeyInfo &km = model.keyInfo[static_cast<uint8_t>(key)];

    // Base cost
    ++strokes;
    sum_key_cost += km.baseCost;

    // Same finger/key
    if (last_finger != Finger::None && km.finger == last_finger) {
      sum_same_finger += 1;
    }
    if (last_key != Key::None && key == last_key) {
      sum_same_key += 1;
    }

    // Hand alternation & run length
    if (last_key == Key::None) {
      run_hand = km.hand;
      run_len  = (km.hand == Hand::None ? 0 : 1);
    } else {
      // There was a previous key
      if (km.hand != Hand::None && last_hand != Hand::None) {
        if (km.hand != last_hand) {
          // Alternation: reward, and start a new run on this hand
          sum_alt_bonus += 1.0;
          run_hand = km.hand;
          run_len  = 1;
        } else {
          // Same hand: extend run
          ++run_len;
          if (run_len > RUN_THRESHOLD) {
            sum_run_pen += double(run_len - RUN_THRESHOLD);
          }
        }
      } else {
        run_hand = km.hand;
        run_len  = (km.hand == Hand::None ? 0 : 1);
      }
    }

    // Roll direction (simple)
    // We look at prev_finger → last_finger → current finger (km.finger),
    // but keep it simple: just last_finger -> current finger on same hand.
    if (last_key != Key::None) {
      if (sameHand(last_finger, km.finger)) {
        FingerPosition pos_prev = fingerToPosition(last_finger);
        FingerPosition pos_curr = fingerToPosition(km.finger);
        if (pos_prev != pos_curr  && pos_curr != FingerPosition::None) {
          // Define direction: delta>0 => moving inwards (outer→inner),
          // delta<0 => moving outwards. You can flip semantics if desired.
          int delta = static_cast<uint8_t>(pos_curr) - static_cast<uint8_t>(pos_prev);
          if (delta > 0) {
            sum_roll_good += 1.0;
          } else if (delta < 0) {
            sum_roll_bad += 1.0;
          }
        }
      }
    }

    // Shift the short-term memory
    prev_finger = last_finger;
    last_finger = km.finger;
    last_hand   = km.hand;
    last_key    = key;
  }

  void reset() {
    strokes         = 0;
    sum_key_cost    = 0.0;
    sum_same_finger = 0.0;
    sum_same_key    = 0.0;
    sum_alt_bonus   = 0.0;
    sum_run_pen     = 0.0;
    sum_roll_good   = 0.0;
    sum_roll_bad    = 0.0;

    last_key        = Key::None;
    last_finger     = Finger::None;
    last_hand       = Hand::None;
    prev_finger     = Finger::None;
    run_hand        = Hand::None;
    run_len         = 0;
  }
};
