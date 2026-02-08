#pragma once

#include <array>

#include "Keyboard/KeyedSequence.h"
#include "State/RunningEffort.h"
#include "Optimizer/Config.h"

// Pre-computed RunningEffort for each static KeyedSequence constant.
// Enables O(1) merge instead of O(k) append in the motion optimizer hot path.
class EffortCache {
  std::array<RunningEffort, KS_COUNT> efforts_;

public:
  explicit EffortCache(const Config& cfg) {
    for (int i = 0; i < KS_COUNT; ++i) {
      const KeyedSequence& ks = ksById(static_cast<KSId>(i));
      efforts_[i].append(ks.keys, cfg);
    }
  }

  const RunningEffort& get(KSId id) const {
    return efforts_[static_cast<uint8_t>(id)];
  }
};
