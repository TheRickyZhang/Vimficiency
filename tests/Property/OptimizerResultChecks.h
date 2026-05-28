#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Optimizer/Result.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/Mode.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

// Pick a bounded sample of (startIdx, resultIdx) replay pairs from the
// per-start result buckets. Caps per-iteration replay work at a constant,
// independent of buffer size, so FuzzTest's mutator can amortize over more
// distinct specs. Deterministic per `seed` so failing inputs replay identically.
inline std::vector<std::pair<size_t, size_t>> sampleReplayPairs(
    const std::vector<std::vector<Result>>& buckets,
    size_t maxResultsPerBucket,
    size_t sampleSize,
    uint64_t seed) {
  std::vector<std::pair<size_t, size_t>> candidates;
  for (size_t i = 0; i < buckets.size(); ++i) {
    size_t limit = std::min(maxResultsPerBucket, buckets[i].size());
    for (size_t r = 0; r < limit; ++r) candidates.emplace_back(i, r);
  }
  if (candidates.size() <= sampleSize) return candidates;
  std::mt19937_64 rng(seed);
  std::shuffle(candidates.begin(), candidates.end(), rng);
  candidates.resize(sampleSize);
  return candidates;
}

inline void expectTopResultsReplay(
    NeovimOracle& oracle,
    const std::vector<Result>& results,
    const Lines& initial,
    CursorPos initialPos,
    const Lines& goal,
    size_t maxResults,
    std::string_view context,
    std::optional<CursorPos> goalPos = std::nullopt,
    std::optional<Mode> goalMode = Mode::Normal) {
  if (results.empty()) {
    ADD_FAILURE() << "No results" << (context.empty() ? "" : " for ")
                  << context;
    return;
  }

  size_t limit = std::min(maxResults, results.size());
  for (size_t i = 0; i < limit; i++) {
    std::string resultContext(context);
    if (!resultContext.empty()) resultContext += " ";
    resultContext += "result " + std::to_string(i);
    EXPECT_TRUE(OracleReplay::matches(
        oracle, initial, initialPos, results[i].getSequence().str(),
        goal, goalPos, goalMode, resultContext));
  }
}
