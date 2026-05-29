// tests/Unit/Misc/KeyboardConfigConsistencyTest.cpp
//
// Each keyboard layout assigns every key a (hand, finger). The two must agree:
// RunningEffort uses KeyInfo::hand for hand-alternation but the finger's hand
// (sameHand) for rolls, so a key whose finger belongs to the other hand is
// scored as both a same-hand roll and a cross-hand alternation.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="KeyboardConfigConsistency.*"

#include <gtest/gtest.h>

#include "Keyboard/Config.h"
#include "Keyboard/Finger.h"
#include "Keyboard/Key.h"

TEST(KeyboardConfigConsistency, ColemakSpaceHandMatchesFinger) {
  Config cfg = Config::colemakDh();
  const KeyInfo& space = cfg.keyInfo[static_cast<uint8_t>(Key::Key_Space)];
  EXPECT_EQ(fingerToHand(space.finger), space.hand);
}

TEST(KeyboardConfigConsistency, EveryKeyHandMatchesFinger) {
  struct Layout {
    const char* name;
    Config cfg;
  };
  Layout layouts[] = {
      {"qwerty", Config::qwerty()},
      {"colemakDh", Config::colemakDh()},
  };
  for (const auto& layout : layouts) {
    for (int i = 0; i < KEY_COUNT; ++i) {
      const KeyInfo& ki = layout.cfg.keyInfo[i];
      if (ki.hand == Hand::None || ki.finger == Finger::None) continue;
      EXPECT_EQ(fingerToHand(ki.finger), ki.hand)
          << layout.name << " key index " << i;
    }
  }
}
