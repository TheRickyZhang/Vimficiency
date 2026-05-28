#pragma once

#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Types/Mode.h"
#include "Interpreter/EditInterpreter.h"
#include "Keyboard/KeyedSequence.h"
#include "Effort/RunningEffort.h"
#include "Types/CursorPos.h"

// =============================================================================
// Suffix Cache Types - for cross-position sharing in TransformOptimizer
// =============================================================================

// Key for suffix cache: editor state without startIndex. Dot-repeat context is
// stored in SuffixValue so cache entries can still be shared across callers.
//
// `targetCol` is intentionally omitted — see TransformStateKey in
// TransformState.h for the invariant and trip wires. Trip wire #2 there is
// the SuffixCache-specific one: if a stored suffix ever begins (or contains)
// a curswant-preserving motion, this key omits a value the replay depends on.
struct SuffixKey {
  size_t linesHash;
  int lineCount;
  int line;
  int col;
  Mode mode;

  SuffixKey(size_t lh, int lc, int ln, int c, Mode m)
      : linesHash(lh), lineCount(lc), line(ln), col(c), mode(m) {}

  SuffixKey(size_t lh, int lc, CursorPos p, Mode m)
      : linesHash(lh), lineCount(lc), line(p.line), col(p.col), mode(m) {}

  bool operator==(const SuffixKey& other) const {
    return linesHash == other.linesHash && lineCount == other.lineCount
        && line == other.line && col == other.col
        && mode == other.mode;
  }
};

struct SuffixKeyHash {
  size_t operator()(const SuffixKey& k) const {
    size_t h = k.linesHash;
    h ^= std::hash<int>{}(k.lineCount) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.line) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.col) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(static_cast<int>(k.mode)) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

// Shared suffix program for one replayed search result.
// Stores parsed commands once, then materializes full suffixes lazily by index.
struct SuffixProgram {
  std::vector<KeyedSequence> editCmds;
  KeyedSequence goalSuffix;
  mutable std::vector<std::optional<KeyedSequence>> memoizedSuffixes;

  SuffixProgram(std::vector<KeyedSequence> cmds, KeyedSequence goal)
      : editCmds(std::move(cmds)),
        goalSuffix(std::move(goal)),
        memoizedSuffixes(editCmds.size() + 1) {}

  int size() const { return static_cast<int>(editCmds.size()); }

  const KeyedSequence& suffixFrom(int startIndex) const {
    assert(startIndex >= 0 && startIndex <= size());
    auto& slot = memoizedSuffixes[startIndex];
    if (!slot.has_value()) {
      if (startIndex == size()) {
        slot = goalSuffix;
      } else {
        KeyedSequence ks = editCmds[startIndex];
        ks += suffixFrom(startIndex + 1);
        slot = std::move(ks);
      }
    }
    return *slot;
  }
};

struct SuffixVariant {
  int startIndex = -1;
  KeyedSequence prefix;
  RunningEffort effort;
  mutable std::optional<KeyedSequence> memoizedPrefixedSuffix;

  const KeyedSequence& suffix(const SuffixProgram& program) const {
    assert(startIndex >= 0);

    // Most variants are just "program suffix from i" with no prefix.
    if (prefix.seq.empty() && prefix.keys.empty()) {
      return program.suffixFrom(startIndex);
    }

    if (!memoizedPrefixedSuffix.has_value()) {
      KeyedSequence ks = prefix;
      ks += program.suffixFrom(startIndex);
      memoizedPrefixedSuffix = std::move(ks);
    }
    return *memoizedPrefixedSuffix;
  }
};

struct SuffixValue {
  std::shared_ptr<const SuffixProgram> program;
  SuffixVariant expandedVariant;
  struct DotOverride {
    std::string matchCmd;
    int startIndex = -1;
    RunningEffort effort;
  };
  // Possible optimization (medium confidence): replace per-entry heap allocation
  // with an arena/pool only if profiling shows dotOverride allocations are hot.
  std::unique_ptr<DotOverride> dotOverride;

  SuffixValue() = default;

  // Plain entry: no leading dot special-case.
  SuffixValue(std::shared_ptr<const SuffixProgram> program,
              int expandedStartIndex, const RunningEffort& expandedEffort)
      : program(std::move(program)) {
    expandedVariant.startIndex = expandedStartIndex;
    expandedVariant.effort = expandedEffort;
  }

  // Entry where the first dot before any dot-resetting edit was expanded into
  // an explicit command. dotOverride keeps the original compact variant for
  // callers whose last-edit context still matches.
  SuffixValue(std::shared_ptr<const SuffixProgram> program,
              int expandedStartIndex, KeyedSequence expandedPrefix,
              const RunningEffort& expandedEffort,
              std::string dotMatchCmd,
              int dotStartIndex, const RunningEffort& dotEffort)
      : program(std::move(program)) {
    expandedVariant.startIndex = expandedStartIndex;
    expandedVariant.prefix = std::move(expandedPrefix);
    expandedVariant.effort = expandedEffort;
    dotOverride = std::make_unique<DotOverride>(
        DotOverride{std::move(dotMatchCmd), dotStartIndex, dotEffort});
  }

  bool canUseDot(const DotRepeat& lastEdit) const {
    if (!dotOverride) return false;
    return matchesCountedCmd(dotOverride->matchCmd, lastEdit.count, lastEdit.base);
  }

  const KeyedSequence& suffix(bool useDot = false) const {
    assert(program != nullptr);
    if (useDot && dotOverride) {
      return program->suffixFrom(dotOverride->startIndex);
    }
    return expandedVariant.suffix(*program);
  }

  const RunningEffort& suffixEffort(bool useDot = false) const {
    if (useDot && dotOverride) {
      return dotOverride->effort;
    }
    return expandedVariant.effort;
  }

private:
  static bool matchesCountedCmd(std::string_view full, int cnt, std::string_view base) {
    if (cnt == 0) return full == base;
    std::string countStr = std::to_string(cnt);
    return full.size() == countStr.size() + base.size() &&
           full.substr(0, countStr.size()) == countStr &&
           full.substr(countStr.size()) == base;
  }
};

using SuffixCacheMap = std::unordered_map<SuffixKey, SuffixValue, SuffixKeyHash>;
