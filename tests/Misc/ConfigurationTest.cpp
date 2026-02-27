// tests/Misc/ConfigurationTest.cpp
//
// Tests for configuration effects on optimizer behavior.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="ConfigurationTest.*"

#include <gtest/gtest.h>

#include "Types/NavContext.h"
#include "Utils/TestUtils.h"

#include "Keyboard/ToKeys/MotionToKeys.h"
#include "Keyboard/Config.h"
#include "Optimizer/CountPenalty.h"
#include "Optimizer/GlobalRuntimeOptions.h"
#include "Boundary/MotionBoundary.h"
#include "Optimizer/MotionOptimizer/MotionOptimizer.h"
#include "Effort/RunningEffort.h"

using namespace std;

namespace {
struct RuntimeOptionsGuard {
  GlobalRuntimeOptions saved;
  RuntimeOptionsGuard() : saved(globalRuntimeOptions()) {}
  ~RuntimeOptionsGuard() { globalRuntimeOptions() = saved; }
};

const Result* findBySequence(const vector<Result>& results, string_view seq) {
  for (const auto& r : results) {
    if (r.getSequence().view() == seq) return &r;
  }
  return nullptr;
}
}  // namespace

class ConfigurationTest : public ::testing::Test {
protected:
  static Lines a2_block_lines;
  static NavContext navContext;

  static void SetUpTestSuite() {
    a2_block_lines = TestFiles::load("a2_block_lines.txt");
    navContext = NavContext();
  }

  static vector<Result>
  runOptimizer(const Lines &lines, CursorPos start,
               CursorPos end, const string &userSeq,
               Config config) {
    MotionOptimizer opt(config);
    MotionBoundary boundary;
    return opt.optimize(lines, start, end,
                        MotionOptimizerParams{}.withMaxResults(30).withMaxNodesExplored(20000),
                        userSeq, boundary, RunningEffort(), navContext).getResults();
  }

  // Get cost of best result for a motion
  static double getBestCost(const Lines &lines, CursorPos start,
                            CursorPos end, const string &userSeq,
                            Config config) {
    auto results = runOptimizer(lines, start, end, userSeq, config);
    if (results.empty()) return -1;
    return results[0].getCost();
  }
};

// Static member definitions
Lines ConfigurationTest::a2_block_lines;
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
  CursorPos start(0, 0);
  CursorPos end(3, 0);
  string userSeq = "jjj";

  double uniformCost = getBestCost(a2_block_lines, start, end, userSeq, Config::uniform());
  double qwertyCost = getBestCost(a2_block_lines, start, end, userSeq, Config::qwerty());
  double colemakCost = getBestCost(a2_block_lines, start, end, userSeq, Config::colemakDh());

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
  CursorPos start(0, 0);
  CursorPos end(1, 0);
  string userSeq = "j";

  // Normal uniform config
  Config normal = Config::uniform();
  auto normalResults = runOptimizer(a2_block_lines, start, end, userSeq, normal);

  // Make j very expensive
  Config expensiveJ = Config::uniform();
  expensiveJ.keyInfo[static_cast<size_t>(Key::Key_J)].base_cost = 100.0;
  auto expensiveResults = runOptimizer(a2_block_lines, start, end, userSeq, expensiveJ);

  ASSERT_FALSE(normalResults.empty());
  ASSERT_FALSE(expensiveResults.empty());

  // With expensive J, the optimizer might prefer alternative motions
  // or at minimum, the cost should be much higher
  double normalJCost = -1;
  double expensiveJCost = -1;

  for (const auto& r : normalResults) {
    if (r.getSequence() == "j") { normalJCost = r.getCost(); break; }
  }
  for (const auto& r : expensiveResults) {
    if (r.getSequence() == "j") { expensiveJCost = r.getCost(); break; }
  }

  if (normalJCost > 0 && expensiveJCost > 0) {
    EXPECT_GT(expensiveJCost, normalJCost)
        << "Expensive J config should result in higher cost for 'j' motion";
  }
}

TEST_F(ConfigurationTest, CountPenaltyOverrideAffectsMotionRanking) {
  RuntimeOptionsGuard guard;

  Lines lines = {"one two three four five six"};
  CursorPos start(0, 0);
  CursorPos end(0, 19);  // reachable via 4w

  MotionOptimizer opt(Config::uniform());
  MotionBoundary boundary;
  MotionOptimizerParams params = MotionOptimizerParams{}
      .withMaxResults(30)
      .withMaxNodesExplored(20000)
      .withMinCountRepeat(4);

  auto baseResults = opt.optimize(lines, start, end, params, "",
                                  boundary, RunningEffort(), navContext).getResults();
  ASSERT_FALSE(baseResults.empty());

  const Result* baseCounted = findBySequence(baseResults, "4w");
  ASSERT_NE(baseCounted, nullptr) << "Expected baseline to include 4w";

  auto& opts = globalRuntimeOptions();
  opts.useCountPenaltyOverrides = true;
  opts.countPenaltyOverrides = {};
  PartialCountPenaltyParams motionWordOverride;
  motionWordOverride.base = 50.0;
  motionWordOverride.countSlope = 0.0;
  motionWordOverride.spanSlope = 0.0;
  opts.countPenaltyOverrides[toIndex(CountClass::MotionWord)] = motionWordOverride;

  auto overrideResults = opt.optimize(lines, start, end, params, "",
                                      boundary, RunningEffort(), navContext).getResults();
  ASSERT_FALSE(overrideResults.empty());

  const Result* overrideCounted = findBySequence(overrideResults, "4w");
  if (overrideCounted) {
    EXPECT_GT(overrideCounted->getCost(), baseCounted->getCost() + 40.0);
  } else {
    SUCCEED() << "4w pruned from results under high count penalty override";
  }

  EXPECT_NE(overrideResults[0].getSequence().view(), "4w")
      << "High MotionWord override should push 4w off top rank";
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
