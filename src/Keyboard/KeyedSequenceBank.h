#pragma once

#include "KeyedSequence.h"
#include "Optimizer/Config.h"

#include <array>

// Pre-computed KeyedSequences with embedded RunningEffort for each static constant.
// Replaces EffortCache: effort travels with the sequence data, enabling
// O(1) merge in the motion optimizer hot path via ks.hasEffort().
struct KeyedSequenceBank {
  // Named members — one per static KeyedSequence constant
#define KS_BANK_MEMBER(name, seq, keys) KeyedSequence name;
  VIMFICIENCY_KEYED_SEQUENCES(KS_BANK_MEMBER)
#undef KS_BANK_MEMBER

  // Pointer table for generic KSId lookup (used by spec-table-driven methods)
  std::array<const KeyedSequence*, KS_COUNT> byIdTable_;

  explicit KeyedSequenceBank(const Config& cfg) {
#define KS_BANK_INIT(name, seqStr, keyGroup) \
    name = KeyedSequence::name; \
    name.effort.append(name.keys, cfg); \
    byIdTable_[static_cast<uint8_t>(KSId::name)] = &this->name;
    VIMFICIENCY_KEYED_SEQUENCES(KS_BANK_INIT)
#undef KS_BANK_INIT
  }

  const KeyedSequence& byId(KSId id) const {
    return *byIdTable_[static_cast<uint8_t>(id)];
  }
};
