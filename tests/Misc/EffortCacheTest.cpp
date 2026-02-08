// tests/Misc/EffortCacheTest.cpp
//
// Verifies that EffortCache pre-computed efforts match naive append, and that
// merge(prefix, cache.get(id)) == prefix.append(ks.keys, cfg) for all 27.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="EffortCache*"

#include <gtest/gtest.h>

#include "Keyboard/KeyedSequence.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "State/EffortCache.h"
#include "State/MotionState.h"
#include "State/RunningEffort.h"

using namespace std;

class EffortCacheTest : public ::testing::Test {
protected:
  Config qwerty  = Config::qwerty();
  Config colemak = Config::colemakDh();
  Config uniform = Config::uniform();
};

// =============================================================================
// All 27 cache entries match naive append
// =============================================================================

TEST_F(EffortCacheTest, AllEntriesMatchNaiveQwerty) {
  EffortCache cache(qwerty);
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    RunningEffort naive;
    naive.append(ks.keys, qwerty);

    EXPECT_DOUBLE_EQ(naive.getEffort(qwerty), cache.get(id).getEffort(qwerty))
        << "Mismatch for KSId " << i << " (seq: " << ks.seq.keys << ")";
    EXPECT_EQ(naive.getStrokes(), cache.get(id).getStrokes())
        << "Stroke mismatch for KSId " << i;
  }
}

TEST_F(EffortCacheTest, AllEntriesMatchNaiveColemak) {
  EffortCache cache(colemak);
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    RunningEffort naive;
    naive.append(ks.keys, colemak);

    EXPECT_DOUBLE_EQ(naive.getEffort(colemak), cache.get(id).getEffort(colemak))
        << "Mismatch for KSId " << i;
  }
}

TEST_F(EffortCacheTest, AllEntriesMatchNaiveUniform) {
  EffortCache cache(uniform);
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    RunningEffort naive;
    naive.append(ks.keys, uniform);

    EXPECT_DOUBLE_EQ(naive.getEffort(uniform), cache.get(id).getEffort(uniform))
        << "Mismatch for KSId " << i;
  }
}

// =============================================================================
// merge(prefix, cache.get(id)) == prefix.append(ks.keys, cfg) for all 27
// =============================================================================

TEST_F(EffortCacheTest, MergeWithPrefixMatchesAppendForAll) {
  EffortCache cache(qwerty);

  // Build a non-trivial prefix: "3w" (multi-key, cross-hand)
  PhysicalKeys prefixKeys = globalTokenizer().tokenize("3w");
  RunningEffort prefix;
  prefix.append(prefixKeys, qwerty);

  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    // Naive: sequential append
    RunningEffort naive = prefix;
    naive.append(ks.keys, qwerty);

    // Cached: merge
    RunningEffort merged = RunningEffort::merge(prefix, cache.get(id));

    EXPECT_DOUBLE_EQ(naive.getEffort(qwerty), merged.getEffort(qwerty))
        << "merge mismatch for KSId " << i << " (seq: " << ks.seq.keys << ")";
    EXPECT_EQ(naive.getStrokes(), merged.getStrokes())
        << "Stroke mismatch for KSId " << i;
  }
}

// =============================================================================
// hasCachedId is correct
// =============================================================================

TEST_F(EffortCacheTest, StaticConstantsHaveCachedId) {
  // All 27 static constants should have cached IDs
  EXPECT_TRUE(KeyedSequence::h.hasCachedId());
  EXPECT_TRUE(KeyedSequence::j.hasCachedId());
  EXPECT_TRUE(KeyedSequence::w.hasCachedId());
  EXPECT_TRUE(KeyedSequence::gg.hasCachedId());
  EXPECT_TRUE(KeyedSequence::Dollar.hasCachedId());
  EXPECT_TRUE(KeyedSequence::CtrlD.hasCachedId());
  EXPECT_TRUE(KeyedSequence::gE.hasCachedId());
}

TEST_F(EffortCacheTest, DynamicSequencesLackCachedId) {
  // Dynamically built KeyedSequences should NOT have a cached ID
  KeyedSequence dynamic("fx", {Key::Key_F, Key::Key_X});
  EXPECT_FALSE(dynamic.hasCachedId());

  KeyedSequence empty;
  EXPECT_FALSE(empty.hasCachedId());
}

// =============================================================================
// ksById round-trip
// =============================================================================

TEST_F(EffortCacheTest, KsByIdRoundTrip) {
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);
    EXPECT_EQ(ks.id, id) << "ksById(" << i << ") has wrong id";
    EXPECT_TRUE(ks.hasCachedId());
    EXPECT_FALSE(ks.keys.empty()) << "KSId " << i << " has empty keys";
  }
}

// =============================================================================
// afterMotion with cache == without cache
// =============================================================================

TEST_F(EffortCacheTest, AfterMotionWithCacheMatchesWithout) {
  EffortCache cache(qwerty);

  // Starting state with some prefix effort
  PhysicalKeys prefixKeys = globalTokenizer().tokenize("2j");
  RunningEffort prefixEffort;
  double initialEffort = prefixEffort.append(prefixKeys, qwerty);

  MotionState state(Position(2, 0, 0), prefixEffort, initialEffort, 0.0);

  // Test a few static motions
  struct TestCase { const KeyedSequence& ks; Position endpoint; };
  TestCase cases[] = {
    {KeyedSequence::w,      Position(2, 5, 5)},
    {KeyedSequence::j,      Position(3, 0, 0)},
    {KeyedSequence::Dollar, Position(2, 10, TARGETCOL_EOL)},
    {KeyedSequence::gg,     Position(0, 0, 0)},
    {KeyedSequence::gE,     Position(1, 3, 3)},
  };

  for (const auto& tc : cases) {
    MotionState withCache = state.afterMotion(tc.ks, tc.endpoint, qwerty, &cache);
    MotionState withoutCache = state.afterMotion(tc.ks, tc.endpoint, qwerty, nullptr);

    EXPECT_DOUBLE_EQ(withCache.getEffort(), withoutCache.getEffort())
        << "Effort mismatch for seq: " << tc.ks.seq.keys;
    EXPECT_EQ(withCache.getRunningEffort().getStrokes(),
              withoutCache.getRunningEffort().getStrokes())
        << "Stroke mismatch for seq: " << tc.ks.seq.keys;
  }
}
