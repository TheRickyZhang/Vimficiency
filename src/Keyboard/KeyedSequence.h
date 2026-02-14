#pragma once

#include "KeyboardModel.h"
#include "CharToKeys.h"
#include "XMacroKeyedSequenceDefinitions.h"
#include "State/Sequence.h"

#include <array>
#include <string>
#include <string_view>

// =============================================================================
// KSId: compile-time identifier for each static KeyedSequence constant
// =============================================================================

#define KS_ENUM_VALUE(name, seq, keys) name,
enum class KSId : uint8_t {
  VIMFICIENCY_KEYED_SEQUENCES(KS_ENUM_VALUE)
  COUNT
};
#undef KS_ENUM_VALUE

static constexpr int KS_COUNT = static_cast<int>(KSId::COUNT);
static_assert(KS_COUNT == 43, "Expected 43 static KeyedSequence constants");

// =============================================================================
// KeyedSequence
// =============================================================================

struct KeyedSequence {
  Sequence seq;
  PhysicalKeys keys;

  KeyedSequence() = default;
  KeyedSequence(std::string_view s, PhysicalKeys k) : seq(std::string(s)), keys(std::move(k)) {}
  KeyedSequence(int count, const KeyedSequence& base) {
    count = std::max(count, 1);
    seq.append(std::to_string(count));
    seq.append(base.seq.view());
    keys.append(makeCountedKeys(count, base.keys));
  }

  KeyedSequence& operator+=(const KeyedSequence& other) {
    seq.append(other.seq.view());
    keys += other.keys;
    return *this;
  }

  void appendChar(char c, int count = 1) {
    seq.append(count, c);
    keys.append(CHAR_TO_KEYS.at(c), count);
  }

  void appendText(std::string_view text) {
    for (char c : text) {
      seq.append(c);
      keys.append(CHAR_TO_KEYS.at(c));
    }
  }

  void appendRepeated(const KeyedSequence& ks, int count) {
    seq.append(count, ks.seq.view());
    keys.append(ks.keys, count);
  }

  void appendCounted(int count, const KeyedSequence& base) {
    seq.append(std::to_string(count));
    seq.append(base.seq.view());
    keys.append(makeCountedKeys(count, base.keys));
  }

  // Convert delete command to its change equivalent (d→c in both seq and keys).
  KeyedSequence asChange() const {
    assert(!seq.view().empty() && seq.view()[0] == 'd');
    return {std::string("c") + std::string(seq.view().substr(1)), keys.asChange()};
  }

  // Static constants — declared via X-macro
#define KS_DECLARE(name, seq, keys) static const KeyedSequence name;
  VIMFICIENCY_KEYED_SEQUENCES(KS_DECLARE)
#undef KS_DECLARE
};

// Static constant definitions — generated via X-macro
#define KS_DEFINE(name, seqStr, keyGroup) \
  inline const KeyedSequence KeyedSequence::name{seqStr, {STRIP_PARENS(keyGroup)}};
VIMFICIENCY_KEYED_SEQUENCES(KS_DEFINE)
#undef KS_DEFINE

// =============================================================================
// ksById: O(1) lookup from KSId to const KeyedSequence&
// =============================================================================

inline const KeyedSequence& ksById(KSId id) {
  // Flat array of pointers, initialized once via X-macro
#define KS_PTR(name, seq, keys) &KeyedSequence::name,
  static const std::array<const KeyedSequence*, KS_COUNT> table = {{
    VIMFICIENCY_KEYED_SEQUENCES(KS_PTR)
  }};
#undef KS_PTR
  return *table[static_cast<uint8_t>(id)];
}
