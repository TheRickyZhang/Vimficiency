// tests/Misc/KeyedSequenceBankTest.cpp
//
// Verifies that KeyedSequenceBank pre-computed efforts match naive append, and
// that merge(prefix, bank.byId(id).effort) == prefix.append(ks.keys, cfg)
// for all 27 static KeyedSequences.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="KeyedSequenceBank*"

#include <gtest/gtest.h>

#include "Keyboard/KeyedSequence.h"
#include "Keyboard/KeyedSequenceBank.h"
#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "State/MotionState.h"
#include "State/RunningEffort.h"

using namespace std;

class KeyedSequenceBankTest : public ::testing::Test {
protected:
  Config qwerty  = Config::qwerty();
  Config colemak = Config::colemakDh();
  Config uniform = Config::uniform();
};

// =============================================================================
// All 27 bank entries match naive append
// =============================================================================

TEST_F(KeyedSequenceBankTest, AllEntriesMatchNaiveQwerty) {
  KeyedSequenceBank bank(qwerty);
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    RunningEffort naive;
    naive.append(ks.keys, qwerty);

    EXPECT_DOUBLE_EQ(naive.getEffort(qwerty), bank.byId(id).effort.getEffort(qwerty))
        << "Mismatch for KSId " << i << " (seq: " << ks.seq.keys << ")";
    EXPECT_EQ(naive.getStrokes(), bank.byId(id).effort.getStrokes())
        << "Stroke mismatch for KSId " << i;
  }
}

TEST_F(KeyedSequenceBankTest, AllEntriesMatchNaiveColemak) {
  KeyedSequenceBank bank(colemak);
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    RunningEffort naive;
    naive.append(ks.keys, colemak);

    EXPECT_DOUBLE_EQ(naive.getEffort(colemak), bank.byId(id).effort.getEffort(colemak))
        << "Mismatch for KSId " << i;
  }
}

TEST_F(KeyedSequenceBankTest, AllEntriesMatchNaiveUniform) {
  KeyedSequenceBank bank(uniform);
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);

    RunningEffort naive;
    naive.append(ks.keys, uniform);

    EXPECT_DOUBLE_EQ(naive.getEffort(uniform), bank.byId(id).effort.getEffort(uniform))
        << "Mismatch for KSId " << i;
  }
}

// =============================================================================
// merge(prefix, bank.byId(id).effort) == prefix.append(ks.keys, cfg) for all 27
// =============================================================================

TEST_F(KeyedSequenceBankTest, MergeWithPrefixMatchesAppendForAll) {
  KeyedSequenceBank bank(qwerty);

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

    // Bank: merge
    RunningEffort merged = RunningEffort::merge(prefix, bank.byId(id).effort);

    EXPECT_DOUBLE_EQ(naive.getEffort(qwerty), merged.getEffort(qwerty))
        << "merge mismatch for KSId " << i << " (seq: " << ks.seq.keys << ")";
    EXPECT_EQ(naive.getStrokes(), merged.getStrokes())
        << "Stroke mismatch for KSId " << i;
  }
}

// =============================================================================
// hasEffort: bank entries vs raw statics
// =============================================================================

TEST_F(KeyedSequenceBankTest, BankEntriesHaveEffort) {
  KeyedSequenceBank bank(qwerty);
  EXPECT_TRUE(bank.h.hasEffort());
  EXPECT_TRUE(bank.j.hasEffort());
  EXPECT_TRUE(bank.w.hasEffort());
  EXPECT_TRUE(bank.gg.hasEffort());
  EXPECT_TRUE(bank.Dollar.hasEffort());
  EXPECT_TRUE(bank.CtrlD.hasEffort());
  EXPECT_TRUE(bank.gE.hasEffort());
}

TEST_F(KeyedSequenceBankTest, StaticConstantsLackEffort) {
  // Global statics should NOT have effort populated
  EXPECT_FALSE(KeyedSequence::h.hasEffort());
  EXPECT_FALSE(KeyedSequence::w.hasEffort());
  EXPECT_FALSE(KeyedSequence::gg.hasEffort());
}

// =============================================================================
// hasCachedId is correct
// =============================================================================

TEST_F(KeyedSequenceBankTest, StaticConstantsHaveCachedId) {
  // All 27 static constants should have cached IDs
  EXPECT_TRUE(KeyedSequence::h.hasCachedId());
  EXPECT_TRUE(KeyedSequence::j.hasCachedId());
  EXPECT_TRUE(KeyedSequence::w.hasCachedId());
  EXPECT_TRUE(KeyedSequence::gg.hasCachedId());
  EXPECT_TRUE(KeyedSequence::Dollar.hasCachedId());
  EXPECT_TRUE(KeyedSequence::CtrlD.hasCachedId());
  EXPECT_TRUE(KeyedSequence::gE.hasCachedId());
}

TEST_F(KeyedSequenceBankTest, DynamicSequencesLackCachedId) {
  // Dynamically built KeyedSequences should NOT have a cached ID
  KeyedSequence dynamic("fx", {Key::Key_F, Key::Key_X});
  EXPECT_FALSE(dynamic.hasCachedId());

  KeyedSequence empty;
  EXPECT_FALSE(empty.hasCachedId());
}

// =============================================================================
// ksById round-trip
// =============================================================================

TEST_F(KeyedSequenceBankTest, KsByIdRoundTrip) {
  for (int i = 0; i < KS_COUNT; ++i) {
    KSId id = static_cast<KSId>(i);
    const KeyedSequence& ks = ksById(id);
    EXPECT_EQ(ks.id, id) << "ksById(" << i << ") has wrong id";
    EXPECT_TRUE(ks.hasCachedId());
    EXPECT_FALSE(ks.keys.empty()) << "KSId " << i << " has empty keys";
  }
}

// =============================================================================
// afterMotion with bank-populated ks == with raw static ks
// =============================================================================

TEST_F(KeyedSequenceBankTest, AfterMotionWithBankMatchesWithout) {
  KeyedSequenceBank bank(qwerty);

  // Starting state with some prefix effort
  PhysicalKeys prefixKeys = globalTokenizer().tokenize("2j");
  RunningEffort prefixEffort;
  double initialEffort = prefixEffort.append(prefixKeys, qwerty);

  MotionState state(Position(2, 0, 0), prefixEffort, initialEffort, 0.0);

  // Test a few static motions: bank entry (has effort) vs raw static (no effort)
  struct TestCase { KSId id; Position endpoint; };
  TestCase cases[] = {
    {KSId::w,      Position(2, 5, 5)},
    {KSId::j,      Position(3, 0, 0)},
    {KSId::Dollar, Position(2, 10, TARGETCOL_EOL)},
    {KSId::gg,     Position(0, 0, 0)},
    {KSId::gE,     Position(1, 3, 3)},
  };

  for (const auto& tc : cases) {
    const KeyedSequence& bankKs = bank.byId(tc.id);
    const KeyedSequence& rawKs = ksById(tc.id);

    MotionState withBank = state.afterMotion(bankKs, tc.endpoint, qwerty);
    MotionState withoutBank = state.afterMotion(rawKs, tc.endpoint, qwerty);

    EXPECT_DOUBLE_EQ(withBank.getEffort(), withoutBank.getEffort())
        << "Effort mismatch for seq: " << rawKs.seq.keys;
    EXPECT_EQ(withBank.getRunningEffort().getStrokes(),
              withoutBank.getRunningEffort().getStrokes())
        << "Stroke mismatch for seq: " << rawKs.seq.keys;
  }
}
