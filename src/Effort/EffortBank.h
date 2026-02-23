#pragma once

#include <array>

#include "Effort/RunningEffort.h"
#include "Keyboard/Config.h"
#include "Keyboard/KeyedSequence.h"

// Pre-computed RunningEffort for each static KeyedSequence constant (indexed by KSId).
// Avoids recomputing effort from keys on every emitMotion call.
struct EffortBank {
  std::array<RunningEffort, KS_COUNT> efforts;

  explicit EffortBank(const Config& config) {
    for (int i = 0; i < KS_COUNT; ++i) {
      efforts[i] = RunningEffort(KeyedSequence::byId(static_cast<KSId>(i)).keys, config);
    }
  }

  const RunningEffort& operator[](KSId id) const {
    return efforts[static_cast<uint8_t>(id)];
  }
};
