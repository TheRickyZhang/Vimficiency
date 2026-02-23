#pragma once

#include "PhysicalKeys.h"
#include "Keyboard/ToKeys/CountToKeys.h"
#include "Keyboard/ToKeys/CharToKeys.h"
#include "XMacroKeyedSequence.h"
#include "VimTypes/Sequence.h"

#include <array>
#include <string>
#include <string_view>

// Note the intentional declaration order in this file with KSId and X-Macro expansions

#define KS_ENUM_VALUE(name, seq, keys) name,
enum class KSId : uint8_t {
  VIMFICIENCY_KEYED_SEQUENCES(KS_ENUM_VALUE)
  COUNT
};
#undef KS_ENUM_VALUE

static constexpr int KS_COUNT = static_cast<int>(KSId::COUNT);
static_assert(KS_COUNT == 61, "Expected 61 static KeyedSequence constants");

// =============================================================================
// KeyedSequence
// =============================================================================

struct KeyedSequence {
  Sequence seq;
  PhysicalKeys keys;

  KeyedSequence() = default;
  KeyedSequence(std::string_view s, PhysicalKeys k) : seq(std::string(s)), keys(std::move(k)) {}
  KeyedSequence(int count, const KeyedSequence& base) {
    appendCounted(count, base);
  }

  KeyedSequence& operator+=(const KeyedSequence& other) {
    seq.append(other.seq.view());
    keys += other.keys;
    return *this;
  }

  // Primitive appends
  void append(char c, int count = 1) {
    seq.append(c, count);
    keys.append(CHAR_TO_KEYS.at(c), count);
  }
  void append(std::string_view text) {
    seq.append(text);
    keys.reserve(keys.size() + text.size());
    // Don't move to PhysicalKeys since it creates a circular dependency?
    for (char c : text) {
      keys.append(CHAR_TO_KEYS.at(c));
    }
  }

  // Same type appends
  void append(const KeyedSequence& ks, int count) {
    seq.append(ks.seq.view(), count);
    keys.append(ks.keys, count);
  }

  void appendCounted(int count, const KeyedSequence& base) {
    assert(count >= 2 && count <= MAX_PREFIX_COUNT);
    seq.append(CountToKeys::textForCount(count));
    seq.append(base.seq.view());
    keys.append(CountToKeys::keysForCount(count));
    keys.append(base.keys);
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

  // KeyedSequence::byId: KSId -> const predefined KeyedSequence&
  static const KeyedSequence& byId(KSId id) {
    // Flat array of pointers, initialized once via X-macro
#define KS_PTR(name, seq, keys) &KeyedSequence::name,
    static const std::array<const KeyedSequence*, KS_COUNT> table = {{
      VIMFICIENCY_KEYED_SEQUENCES(KS_PTR)
    }};
#undef KS_PTR
    return *table[static_cast<uint8_t>(id)];
  }
};

// =============================================================================
// KSId: compile-time identifier for each static KeyedSequence constant
// =============================================================================

// Static constant definitions — generated via X-macro
#define KS_DEFINE(name, seqStr, keyGroup) \
  inline const KeyedSequence KeyedSequence::name{seqStr, {STRIP_PARENS(keyGroup)}};
VIMFICIENCY_KEYED_SEQUENCES(KS_DEFINE)
#undef KS_DEFINE
