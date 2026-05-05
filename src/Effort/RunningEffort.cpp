#include "Effort/RunningEffort.h"

#include "Keyboard/Finger.h"
#include "Keyboard/ToKeys/MovementToKeys.h"

RunningEffort::RunningEffort(const PhysicalKeys& keys, const Config& model) {
  append(keys, model);
}

double RunningEffort::getEffort(const Config &model) const {
  const auto &w = model.weights;

  double s = 0.0;
  s += w.key_weight         * sum_key_cost;
  s += w.same_finger_weight * sum_same_finger;
  s += w.same_key_weight    * sum_same_key;
  s += w.alt_hand_weight    * sum_alt_bonus;
  s += w.good_roll_weight   * sum_roll_good;
  s += w.bad_roll_weight    * sum_roll_bad;

  return s + penalty_;
}

// Returns total effort after appending keys
double RunningEffort::append(const PhysicalKeys& keys, const Config& model) {
  for(Key k : keys) {
    appendSingle(k, model);
  }
  return getEffort(model);
}

// Append a single key and update all metrics.
void RunningEffort::appendSingle(Key key, const Config &model) {
  const KeyInfo &km = model.keyInfo[static_cast<uint8_t>(key)];

  // Base cost
  ++strokes;
  sum_key_cost += km.base_cost;

  // Track first key for merge boundary
  if (strokes == 1) {
    first_key    = key;
    first_finger = km.finger;
    first_hand   = km.hand;
  }

  // Same finger/key
  if (last_finger != Finger::None && km.finger == last_finger) {
    sum_same_finger += 1;
  }
  if (last_key != Key::None && key == last_key) {
    sum_same_key += 1;
  }

  // Hand alternation
  if (last_key != Key::None &&
      km.hand != Hand::None && last_hand != Hand::None &&
      km.hand != last_hand) {
    sum_alt_bonus += 1.0;
  }

  // Roll direction: last_finger -> current finger on same hand.
  if (last_key != Key::None && last_finger != Finger::None) {
    if (sameHand(last_finger, km.finger)) {
      FingerPosition pos_prev = fingerToPosition(last_finger);
      FingerPosition pos_curr = fingerToPosition(km.finger);
      if (pos_prev != pos_curr  && pos_curr != FingerPosition::None) {
        int delta = static_cast<uint8_t>(pos_curr) - static_cast<uint8_t>(pos_prev);
        if (delta > 0) {
          sum_roll_good += 1.0;
        } else if (delta < 0) {
          sum_roll_bad += 1.0;
        }
      }
    }
  }

  // Update trailing boundary
  last_finger = km.finger;
  last_hand   = km.hand;
  last_key    = key;
}

void RunningEffort::reset() {
  strokes         = 0;
  sum_key_cost    = 0.0;
  sum_same_finger = 0.0;
  sum_same_key    = 0.0;
  sum_alt_bonus   = 0.0;
  sum_roll_good   = 0.0;
  sum_roll_bad    = 0.0;

  first_key       = Key::None;
  first_finger    = Finger::None;
  first_hand      = Hand::None;
  last_key        = Key::None;
  last_finger     = Finger::None;
  last_hand       = Hand::None;

  penalty_        = 0.0;
}

RunningEffort RunningEffort::merge(const RunningEffort& a, const RunningEffort& b) {
  if (a.strokes == 0) return b;
  if (b.strokes == 0) return a;

  RunningEffort r;

  // Bulk sums are purely additive
  r.strokes         = a.strokes + b.strokes;
  r.sum_key_cost    = a.sum_key_cost    + b.sum_key_cost;
  r.sum_same_finger = a.sum_same_finger + b.sum_same_finger;
  r.sum_same_key    = a.sum_same_key    + b.sum_same_key;
  r.sum_alt_bonus   = a.sum_alt_bonus   + b.sum_alt_bonus;
  r.sum_roll_good   = a.sum_roll_good   + b.sum_roll_good;
  r.sum_roll_bad    = a.sum_roll_bad    + b.sum_roll_bad;

  // Boundary corrections between A's last key and B's first key
  if (a.last_finger != Finger::None && b.first_finger != Finger::None &&
      a.last_finger == b.first_finger) {
    r.sum_same_finger += 1.0;
  }
  if (a.last_key != Key::None && b.first_key != Key::None &&
      a.last_key == b.first_key) {
    r.sum_same_key += 1.0;
  }
  if (a.last_hand != Hand::None && b.first_hand != Hand::None &&
      a.last_hand != b.first_hand) {
    r.sum_alt_bonus += 1.0;
  }

  // Roll at boundary
  if (a.last_finger != Finger::None && b.first_finger != Finger::None &&
      sameHand(a.last_finger, b.first_finger)) {
    FingerPosition pa = fingerToPosition(a.last_finger);
    FingerPosition pb = fingerToPosition(b.first_finger);
    if (pa != pb && pb != FingerPosition::None) {
      int delta = static_cast<uint8_t>(pb) - static_cast<uint8_t>(pa);
      if (delta > 0)      r.sum_roll_good += 1.0;
      else if (delta < 0) r.sum_roll_bad  += 1.0;
    }
  }

  // Propagate boundary metadata
  r.first_key    = a.first_key;
  r.first_finger = a.first_finger;
  r.first_hand   = a.first_hand;
  r.last_key     = b.last_key;
  r.last_finger  = b.last_finger;
  r.last_hand    = b.last_hand;

  r.penalty_     = a.penalty_ + b.penalty_;

  return r;
}

double RunningEffort::appendFrom(const RunningEffort& other, const Config& model) {
  if (other.strokes == 0) return getEffort(model);
  if (strokes == 0) {
    *this = other;
    return getEffort(model);
  }

  // Boundary corrections between this->last and other.first
  if (last_finger != Finger::None && other.first_finger != Finger::None &&
      last_finger == other.first_finger) {
    sum_same_finger += 1.0;
  }
  if (last_key != Key::None && other.first_key != Key::None &&
      last_key == other.first_key) {
    sum_same_key += 1.0;
  }
  if (last_hand != Hand::None && other.first_hand != Hand::None &&
      last_hand != other.first_hand) {
    sum_alt_bonus += 1.0;
  }

  // Roll at boundary
  if (last_finger != Finger::None && other.first_finger != Finger::None &&
      sameHand(last_finger, other.first_finger)) {
    FingerPosition pa = fingerToPosition(last_finger);
    FingerPosition pb = fingerToPosition(other.first_finger);
    if (pa != pb && pb != FingerPosition::None) {
      int delta = static_cast<uint8_t>(pb) - static_cast<uint8_t>(pa);
      if (delta > 0)      sum_roll_good += 1.0;
      else if (delta < 0) sum_roll_bad  += 1.0;
    }
  }

  // Bulk-add sums from other
  strokes         += other.strokes;
  sum_key_cost    += other.sum_key_cost;
  sum_same_finger += other.sum_same_finger;
  sum_same_key    += other.sum_same_key;
  sum_alt_bonus   += other.sum_alt_bonus;
  sum_roll_good   += other.sum_roll_good;
  sum_roll_bad    += other.sum_roll_bad;

  // Update trailing boundary from other
  last_key    = other.last_key;
  last_finger = other.last_finger;
  last_hand   = other.last_hand;

  penalty_    += other.penalty_;

  return getEffort(model);
}

double getEffort(std::string_view seq,
                 const Config      &cfg) {
  PhysicalKeys keys = globalSequenceToKeys().tokenize(seq);
  RunningEffort st(keys, cfg);
  return st.getEffort(cfg);
}
