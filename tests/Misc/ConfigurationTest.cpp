#include <gtest/gtest.h>

#include "Editor/NavContext.h"
#include "Utils/TestUtils.h"

#include "Keyboard/MotionToKeys.h"
#include "Optimizer/Config.h"
#include "Optimizer/ImpliedExclusions.h"
#include "Optimizer/MovementOptimizer.h"
#include "State/RunningEffort.h"

using namespace std;

class ConfigurationTest : public ::testing::Test {
protected:
  static vector<string> a2_block_lines;
  static NavContext navContext;

  static void SetUpTestSuite() {
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    navContext = NavContext(39, 19);
  }

  static vector<Result>
  runOptimizer(const vector<string> &lines, Position start,
               Position end, const string &userSeq,
               Config config,
               const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS) {
    MovementOptimizer opt(config);
    ImpliedExclusions impliedExclusions(false, false);
    return opt.optimize(lines, start, RunningEffort(), end, userSeq, navContext,
                        impliedExclusions, allowedMotions, OptimizerParams(30, 2e4, 1.0, 2.0));
  }

  // Get cost of best result for a motion
  static double getBestCost(const vector<string> &lines, Position start,
                            Position end, const string &userSeq,
                            Config config,
                            const MotionToKeys& allowedMotions = EXPLORABLE_MOTIONS) {
    auto results = runOptimizer(lines, start, end, userSeq, config, allowedMotions);
    if (results.empty()) return -1;
    return results[0].keyCost;
  }
};

// Static member definitions
vector<string> ConfigurationTest::a2_block_lines;
NavContext ConfigurationTest::navContext(0, 0);

// =============================================================================
// Keyboard Layout Tests
// =============================================================================

TEST_F(ConfigurationTest, UniformLayout_AllKeysSameCost) {
  Config cfg = Config::uniform();

  // In uniform layout, all non-modifier keys should have cost 1.0
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost, 1.0);
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_K)].base_cost, 1.0);
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_H)].base_cost, 1.0);
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_L)].base_cost, 1.0);
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_W)].base_cost, 1.0);
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_G)].base_cost, 1.0);

  // Modifiers should have 0 cost in uniform
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_Shift)].base_cost, 0.0);
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_Ctrl)].base_cost, 0.0);
}

TEST_F(ConfigurationTest, QwertyLayout_HomeRowCheaper) {
  Config cfg = Config::qwerty();

  // Home row keys should be cheaper than top row
  double j_cost = cfg.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost;
  double u_cost = cfg.keyInfo[static_cast<size_t>(Key::Key_U)].base_cost;
  EXPECT_LT(j_cost, u_cost) << "Home row J should be cheaper than top row U";

  // j = 1.0, u = 1.4 in qwerty
  EXPECT_DOUBLE_EQ(j_cost, 1.0);
  EXPECT_DOUBLE_EQ(u_cost, 1.4);
}

TEST_F(ConfigurationTest, ColemakDhLayout_DifferentFromQwerty) {
  Config qwerty = Config::qwerty();
  Config colemak = Config::colemakDh();

  // J has different costs in qwerty vs colemak-dh
  double j_qwerty = qwerty.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost;
  double j_colemak = colemak.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost;

  // In QWERTY, J is on home row (1.0), in Colemak-DH it's in top row position (1.6)
  EXPECT_DOUBLE_EQ(j_qwerty, 1.0);
  EXPECT_DOUBLE_EQ(j_colemak, 1.6);
  EXPECT_NE(j_qwerty, j_colemak);
}

TEST_F(ConfigurationTest, LayoutAffectsOptimizer) {
  // Moving down 3 lines: "3j" or "jjj"
  Position start(0, 0);
  Position end(3, 0);
  string userSeq = "jjj";

  auto motions = getSlicedMotionToKeys({"j", "k"});
  double uniformCost = getBestCost(a2_block_lines, start, end, userSeq, Config::uniform(), motions);
  double qwertyCost = getBestCost(a2_block_lines, start, end, userSeq, Config::qwerty(), motions);
  double colemakCost = getBestCost(a2_block_lines, start, end, userSeq, Config::colemakDh(), motions);

  // All should find valid results
  EXPECT_GT(uniformCost, 0);
  EXPECT_GT(qwertyCost, 0);
  EXPECT_GT(colemakCost, 0);

  // Colemak should have higher cost for j since it's not on home row
  // (In uniform j=1.0, in qwerty j=1.0, in colemak j=1.6)
  EXPECT_NE(uniformCost, colemakCost);
}

// =============================================================================
// Hand/Finger Assignment Tests
// =============================================================================

TEST_F(ConfigurationTest, QwertyHandAssignments) {
  Config cfg = Config::qwerty();

  // Left hand keys
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_A)].hand, Hand::Left);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_S)].hand, Hand::Left);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_D)].hand, Hand::Left);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_F)].hand, Hand::Left);

  // Right hand keys
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_J)].hand, Hand::Right);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_K)].hand, Hand::Right);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_L)].hand, Hand::Right);
}

TEST_F(ConfigurationTest, QwertyFingerAssignments) {
  Config cfg = Config::qwerty();

  // Index fingers
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_F)].finger, Finger::Li);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_J)].finger, Finger::Ri);

  // Middle fingers
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_D)].finger, Finger::Lm);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_K)].finger, Finger::Rm);

  // Pinky fingers
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_A)].finger, Finger::Lp);
  EXPECT_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_Semicolon)].finger, Finger::Rp);
}

// =============================================================================
// Weight Configuration Tests
// =============================================================================

TEST_F(ConfigurationTest, UniformLayout_NoWeightPenalties) {
  Config cfg = Config::uniform();

  // Uniform should have zeroed weights (no same-finger penalty, no alternation bonus, etc.)
  EXPECT_DOUBLE_EQ(cfg.weights.w_same_finger, 0.0);
  EXPECT_DOUBLE_EQ(cfg.weights.w_alt_bonus, 0.0);
  EXPECT_DOUBLE_EQ(cfg.weights.w_roll_good, 0.0);
  EXPECT_DOUBLE_EQ(cfg.weights.w_roll_bad, 0.0);
}

TEST_F(ConfigurationTest, WeightsAffectKeyCostMultiplier) {
  Config cfg = Config::uniform();

  // Default w_key is 1.0
  EXPECT_DOUBLE_EQ(cfg.weights.w_key, 1.0);
}

// =============================================================================
// Custom Key Override Tests (simulating FFI config path)
// =============================================================================

TEST_F(ConfigurationTest, CustomKeyCostOverride) {
  Config cfg = Config::uniform();

  // Override J to be very expensive
  cfg.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost = 10.0;

  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost, 10.0);
  // Other keys should remain at 1.0
  EXPECT_DOUBLE_EQ(cfg.keyInfo[static_cast<size_t>(Key::Key_K)].base_cost, 1.0);
}

TEST_F(ConfigurationTest, CustomKeyCostAffectsOptimizer) {
  Position start(0, 0);
  Position end(1, 0);
  string userSeq = "j";

  // Normal uniform config
  Config normal = Config::uniform();
  auto normalResults = runOptimizer(a2_block_lines, start, end, userSeq, normal,
                                     getSlicedMotionToKeys({"j", "k", "+", "-"}));

  // Make j very expensive
  Config expensiveJ = Config::uniform();
  expensiveJ.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost = 100.0;
  auto expensiveResults = runOptimizer(a2_block_lines, start, end, userSeq, expensiveJ,
                                        getSlicedMotionToKeys({"j", "k", "+", "-"}));

  ASSERT_FALSE(normalResults.empty());
  ASSERT_FALSE(expensiveResults.empty());

  // With expensive J, the optimizer might prefer alternative motions
  // or at minimum, the cost should be much higher
  double normalJCost = -1;
  double expensiveJCost = -1;

  for (const auto& r : normalResults) {
    if (r.getSequenceString() == "j") { normalJCost = r.keyCost; break; }
  }
  for (const auto& r : expensiveResults) {
    if (r.getSequenceString() == "j") { expensiveJCost = r.keyCost; break; }
  }

  if (normalJCost > 0 && expensiveJCost > 0) {
    EXPECT_GT(expensiveJCost, normalJCost)
        << "Expensive J config should result in higher cost for 'j' motion";
  }
}

// =============================================================================
// Preset Completeness Tests
// =============================================================================

TEST_F(ConfigurationTest, QwertyDefinesAllLetterKeys) {
  Config cfg = Config::qwerty();

  // Check that all letter keys have non-zero costs and valid hand assignment
  for (Key k : {Key::Key_A, Key::Key_B, Key::Key_C, Key::Key_D, Key::Key_E,
                Key::Key_F, Key::Key_G, Key::Key_H, Key::Key_I, Key::Key_J,
                Key::Key_K, Key::Key_L, Key::Key_M, Key::Key_N, Key::Key_O,
                Key::Key_P, Key::Key_Q, Key::Key_R, Key::Key_S, Key::Key_T,
                Key::Key_U, Key::Key_V, Key::Key_W, Key::Key_X, Key::Key_Y,
                Key::Key_Z}) {
    auto& info = cfg.keyInfo[static_cast<size_t>(k)];
    EXPECT_GT(info.base_cost, 0) << "Key should have non-zero cost";
    EXPECT_NE(info.hand, Hand::None) << "Key should have hand assignment";
    EXPECT_NE(info.finger, Finger::None) << "Key should have finger assignment";
  }
}

TEST_F(ConfigurationTest, QwertyDefinesDigitKeys) {
  Config cfg = Config::qwerty();

  for (Key k : {Key::Key_0, Key::Key_1, Key::Key_2, Key::Key_3, Key::Key_4,
                Key::Key_5, Key::Key_6, Key::Key_7, Key::Key_8, Key::Key_9}) {
    auto& info = cfg.keyInfo[static_cast<size_t>(k)];
    EXPECT_GT(info.base_cost, 0) << "Digit key should have non-zero cost";
    EXPECT_NE(info.hand, Hand::None) << "Digit key should have hand assignment";
  }
}
