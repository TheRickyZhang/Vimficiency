// tests/Misc/EffortCompositionTest.cpp
//
// Verifies that RunningEffort::merge(a, b) produces the same effort as
// sequentially appending all keys of b after a.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="EffortCompositionTest.*"

#include <gtest/gtest.h>

#include "Keyboard/PhysicalKeys.h"
#include "Keyboard/ToKeys/MovementToKeys.h"
#include "Keyboard/Config.h"
#include "Effort/RunningEffort.h"
#include "Utils/RandomGeneration.h"

using namespace std;

namespace {
constexpr double EFFORT_TOLERANCE = 1e-9;
}

class EffortCompositionTest : public ::testing::Test {
protected:
  Config qwerty = Config::qwerty();

  // Build RunningEffort by appending keys
  RunningEffort buildEffort(const PhysicalKeys& keys) {
    RunningEffort e;
    e.append(keys, qwerty);
    return e;
  }

  // Build RunningEffort from a sequence string
  RunningEffort buildEffort(string_view seq) {
    PhysicalKeys keys = globalSequenceToKeys().tokenize(seq);
    RunningEffort e;
    e.append(keys, qwerty);
    return e;
  }

  // Build RunningEffort by appending all keys naively (the ground truth)
  RunningEffort buildNaive(const PhysicalKeys& a, const PhysicalKeys& b) {
    RunningEffort e;
    e.append(a, qwerty);
    e.append(b, qwerty);
    return e;
  }

  void expectMergeEqualsNaive(const PhysicalKeys& aKeys, const PhysicalKeys& bKeys) {
    RunningEffort naive = buildNaive(aKeys, bKeys);
    RunningEffort merged = RunningEffort::merge(buildEffort(aKeys), buildEffort(bKeys));

    double naiveEffort = naive.getEffort(qwerty);
    double mergedEffort = merged.getEffort(qwerty);

    EXPECT_NEAR(naiveEffort, mergedEffort, EFFORT_TOLERANCE)
        << "Merged effort should equal naive sequential effort";
    EXPECT_EQ(naive.getStrokes(), merged.getStrokes())
        << "Stroke counts should match";
  }

  void expectMergeEqualsNaive(string_view aSeq, string_view bSeq) {
    PhysicalKeys aKeys = globalSequenceToKeys().tokenize(aSeq);
    PhysicalKeys bKeys = globalSequenceToKeys().tokenize(bSeq);
    expectMergeEqualsNaive(aKeys, bKeys);
  }

  PhysicalKeys randomPhysicalKeys(int minLen, int maxLen) {
    int len = RandomGen::range(minLen, maxLen);
    PhysicalKeys keys;
    keys.reserve(len);
    for (int i = 0; i < len; i++) {
      keys.push_back(static_cast<Key>(RandomGen::range(0, KEY_COUNT - 1)));
    }
    return keys;
  }

  void expectAssociativeMergeEqualsNaive(
      const PhysicalKeys& aKeys,
      const PhysicalKeys& bKeys,
      const PhysicalKeys& cKeys) {
    RunningEffort a = buildEffort(aKeys);
    RunningEffort b = buildEffort(bKeys);
    RunningEffort c = buildEffort(cKeys);

    RunningEffort ab_c = RunningEffort::merge(RunningEffort::merge(a, b), c);
    RunningEffort a_bc = RunningEffort::merge(a, RunningEffort::merge(b, c));

    RunningEffort naive;
    naive.append(aKeys, qwerty);
    naive.append(bKeys, qwerty);
    naive.append(cKeys, qwerty);

    EXPECT_NEAR(
        naive.getEffort(qwerty), ab_c.getEffort(qwerty), EFFORT_TOLERANCE)
        << "(a+b)+c should match naive";
    EXPECT_NEAR(
        naive.getEffort(qwerty), a_bc.getEffort(qwerty), EFFORT_TOLERANCE)
        << "a+(b+c) should match naive";
    EXPECT_EQ(naive.getStrokes(), ab_c.getStrokes());
    EXPECT_EQ(naive.getStrokes(), a_bc.getStrokes());
  }
};

// =============================================================================
// Identity: merge with empty
// =============================================================================

TEST_F(EffortCompositionTest, MergeWithEmptyLeft) {
  RunningEffort empty;
  RunningEffort b = buildEffort("jjj");
  RunningEffort merged = RunningEffort::merge(empty, b);
  EXPECT_DOUBLE_EQ(b.getEffort(qwerty), merged.getEffort(qwerty));
  EXPECT_EQ(b.getStrokes(), merged.getStrokes());
}

TEST_F(EffortCompositionTest, MergeWithEmptyRight) {
  RunningEffort a = buildEffort("3w");
  RunningEffort empty;
  RunningEffort merged = RunningEffort::merge(a, empty);
  EXPECT_DOUBLE_EQ(a.getEffort(qwerty), merged.getEffort(qwerty));
  EXPECT_EQ(a.getStrokes(), merged.getStrokes());
}

TEST_F(EffortCompositionTest, MergeBothEmpty) {
  RunningEffort empty;
  RunningEffort merged = RunningEffort::merge(empty, empty);
  EXPECT_DOUBLE_EQ(0.0, merged.getEffort(qwerty));
  EXPECT_EQ(0, merged.getStrokes());
}

// =============================================================================
// Same key at boundary (triggers same_key and same_finger corrections)
// =============================================================================

TEST_F(EffortCompositionTest, SameKeyAtBoundary) {
  // "jj" split as "j" + "j" — boundary has same key
  expectMergeEqualsNaive("j", "j");
}

TEST_F(EffortCompositionTest, SameFingerDifferentKey) {
  // J and U are both right index finger in QWERTY
  expectMergeEqualsNaive(
      PhysicalKeys{Key::Key_J},
      PhysicalKeys{Key::Key_U});
}

// =============================================================================
// Hand alternation at boundary
// =============================================================================

TEST_F(EffortCompositionTest, HandAlternationAtBoundary) {
  // 'f' is left hand, 'j' is right hand
  expectMergeEqualsNaive(
      PhysicalKeys{Key::Key_F},
      PhysicalKeys{Key::Key_J});
}

TEST_F(EffortCompositionTest, SameHandAtBoundary) {
  // 'j' and 'k' are both right hand — no alternation bonus
  expectMergeEqualsNaive(
      PhysicalKeys{Key::Key_J},
      PhysicalKeys{Key::Key_K});
}

// =============================================================================
// Roll detection at boundary
// =============================================================================

TEST_F(EffortCompositionTest, GoodRollAtBoundary) {
  // Right hand: pinky -> ring is inward (good roll)
  // Semicolon (Rp) -> L (Rr)
  expectMergeEqualsNaive(
      PhysicalKeys{Key::Key_Semicolon},
      PhysicalKeys{Key::Key_L});
}

TEST_F(EffortCompositionTest, BadRollAtBoundary) {
  // Right hand: ring -> pinky is outward (bad roll)
  // L (Rr) -> Semicolon (Rp)
  expectMergeEqualsNaive(
      PhysicalKeys{Key::Key_L},
      PhysicalKeys{Key::Key_Semicolon});
}

// =============================================================================
// Multi-key sequences
// =============================================================================

TEST_F(EffortCompositionTest, MotionSequences) {
  expectMergeEqualsNaive("3w", "2j");
}

TEST_F(EffortCompositionTest, LongerSequences) {
  expectMergeEqualsNaive("3wfx", "2jll");
}

TEST_F(EffortCompositionTest, RepeatedMotions) {
  expectMergeEqualsNaive("jjj", "kkk");
}

TEST_F(EffortCompositionTest, MixedHands) {
  expectMergeEqualsNaive("fa", "jk");
}

// =============================================================================
// Three-way merge (associativity)
// =============================================================================

TEST_F(EffortCompositionTest, Associativity) {
  RunningEffort a = buildEffort("3w");
  RunningEffort b = buildEffort("j");
  RunningEffort c = buildEffort("2l");

  // (a + b) + c
  RunningEffort ab_c = RunningEffort::merge(RunningEffort::merge(a, b), c);
  // a + (b + c)
  RunningEffort a_bc = RunningEffort::merge(a, RunningEffort::merge(b, c));
  // naive: append all
  PhysicalKeys aKeys = globalSequenceToKeys().tokenize("3w");
  PhysicalKeys bKeys = globalSequenceToKeys().tokenize("j");
  PhysicalKeys cKeys = globalSequenceToKeys().tokenize("2l");
  RunningEffort naive;
  naive.append(aKeys, qwerty);
  naive.append(bKeys, qwerty);
  naive.append(cKeys, qwerty);

  double naiveEffort = naive.getEffort(qwerty);
  EXPECT_NEAR(naiveEffort, ab_c.getEffort(qwerty), EFFORT_TOLERANCE)
      << "(a+b)+c should match naive";
  EXPECT_NEAR(naiveEffort, a_bc.getEffort(qwerty), EFFORT_TOLERANCE)
      << "a+(b+c) should match naive";
}

// =============================================================================
// Append after merge (continuation correctness)
// =============================================================================

TEST_F(EffortCompositionTest, AppendAfterMerge) {
  // Merge a+b, then append more keys — should match fully sequential
  PhysicalKeys aKeys = globalSequenceToKeys().tokenize("3w");
  PhysicalKeys bKeys = globalSequenceToKeys().tokenize("j");
  PhysicalKeys cKeys = globalSequenceToKeys().tokenize("l");

  RunningEffort merged = RunningEffort::merge(buildEffort(aKeys), buildEffort(bKeys));
  merged.append(cKeys, qwerty);

  RunningEffort naive;
  naive.append(aKeys, qwerty);
  naive.append(bKeys, qwerty);
  naive.append(cKeys, qwerty);

  EXPECT_NEAR(
      naive.getEffort(qwerty), merged.getEffort(qwerty), EFFORT_TOLERANCE);
}

// =============================================================================
// Single-key segments
// =============================================================================

TEST_F(EffortCompositionTest, SingleKeySplit) {
  // Every possible split of "jkl" into two parts
  expectMergeEqualsNaive(
      PhysicalKeys{Key::Key_J},
      PhysicalKeys({Key::Key_K, Key::Key_L}));
  expectMergeEqualsNaive(
      PhysicalKeys({Key::Key_J, Key::Key_K}),
      PhysicalKeys{Key::Key_L});
}

// =============================================================================
// Uniform config (no bonuses/penalties — pure key cost)
// =============================================================================

TEST_F(EffortCompositionTest, UniformConfig) {
  Config uniform = Config::uniform();
  PhysicalKeys aKeys = globalSequenceToKeys().tokenize("3w");
  PhysicalKeys bKeys = globalSequenceToKeys().tokenize("2j");

  RunningEffort a, b, naive;
  a.append(aKeys, uniform);
  b.append(bKeys, uniform);
  naive.append(aKeys, uniform);
  naive.append(bKeys, uniform);

  RunningEffort merged = RunningEffort::merge(a, b);
  EXPECT_NEAR(
      naive.getEffort(uniform), merged.getEffort(uniform), EFFORT_TOLERANCE);
}
