#pragma once

#include <string_view>

#include "Keyboard/KeyboardModel.h"
#include "Optimizer/Config.h"

// -----------------------------------------------------------------------------
// Composable effort model (monoid over key sequences).
//
// Tracks typing cost metrics that can be merged in O(1) via
// RunningEffort::merge(a, b), producing the same result as sequentially
// appending all keys of b after a.
// -----------------------------------------------------------------------------

class RunningEffort {
private:
  int    strokes           = 0;
  double sum_key_cost      = 0.0; // ∑ base_cost
  double sum_same_finger   = 0.0; // count/pen for same-finger bigrams
  double sum_same_key      = 0.0; // extra pen for same-key repeats
  double sum_alt_bonus     = 0.0; // count of alternations (to be rewarded)
  double sum_roll_good     = 0.0; // count of "good" rolls
  double sum_roll_bad      = 0.0; // count of "bad" rolls

  // Boundary metadata for monoid composition
  Key    first_key         = Key::None;
  Finger first_finger      = Finger::None;
  Hand   first_hand        = Hand::None;

  Key    last_key          = Key::None;
  Finger last_finger       = Finger::None;
  Hand   last_hand         = Hand::None;

  // Append a single key and update all metrics.
  void appendSingle(Key key, const Config &model);

public:
  double getEffort(const Config &model) const;

  double append(const PhysicalKeys& keys, const Config& model);

  void reset();

  // Merge two effort segments in O(1), computing boundary corrections.
  // merge(a, b) == appending all of b's keys after a sequentially.
  static RunningEffort merge(const RunningEffort& a, const RunningEffort& b);

  int getStrokes() const { return strokes; }
};

double getEffort(std::string_view seq, const Config &cfg);

