#include <gtest/gtest.h>

#include "Optimizer/CountPenalty.h"

TEST(CountPenaltyTest, CountOneOrLessHasNoPenalty) {
  CountPenaltyInput in;
  in.span = 5;

  in.count = 0;
  EXPECT_DOUBLE_EQ(countPenalty<CountClass::MovementWord>(in), 0.0);

  in.count = 1;
  EXPECT_DOUBLE_EQ(countPenalty<CountClass::EditLine>(in), 0.0);
}

TEST(CountPenaltyTest, DefaultSpecUsesBaseCountAndSpan) {
  CountPenaltyInput in;
  in.count = 4;
  in.span = 4;

  // MovementWord default: base=1.0, countSlope=0.5, spanSlope=0.1
  // penalty = 1.0 + 0.5*(4-1) + 0.1*4 = 2.9
  EXPECT_NEAR(countPenalty<CountClass::MovementWord>(in), 2.9, 1e-12);
}

TEST(CountPenaltyTest, NegativeSpanIsClampedToZero) {
  CountPenaltyInput in;
  in.count = 3;
  in.span = -20;

  // MovementWord default with span clamped to 0:
  // 1.0 + 0.5*(3-1) + 0.1*0 = 2.0
  EXPECT_NEAR(countPenalty<CountClass::MovementWord>(in), 2.0, 1e-12);
}

TEST(CountPenaltyTest, OverridesAffectOnlySelectedClass) {
  CountPenaltyOverrideTable overrides{};

  PartialCountPenaltyParams wordOverride;
  wordOverride.base = 3.0;
  wordOverride.countSlope = 2.0;
  wordOverride.spanSlope = 0.0;
  overrides[toIndex(CountClass::MovementWord)] = wordOverride;

  CountPenaltyInput in;
  in.count = 3;
  in.span = 10;

  // Overridden MovementWord: 3.0 + 2.0*(3-1) + 0*10 = 7.0
  EXPECT_NEAR(countPenalty<CountClass::MovementWord>(in, overrides), 7.0, 1e-12);

  // MovementSentence remains default: 1.0 + 0.5*(3-1) + 0.1*10 = 3.0
  EXPECT_NEAR(countPenalty<CountClass::MovementSentence>(in, overrides), 3.0, 1e-12);
}

TEST(CountPenaltyTest, PartialOverrideKeepsUnspecifiedFields) {
  CountPenaltyOverrideTable overrides{};

  PartialCountPenaltyParams editWordOverride;
  editWordOverride.spanSlope = 0.25;
  overrides[toIndex(CountClass::EditWord)] = editWordOverride;

  CountPenaltyInput in;
  in.count = 3;
  in.span = 4;

  // EditWord defaults base=1.0, countSlope=0.5, overridden spanSlope=0.25
  // 1.0 + 0.5*(3-1) + 0.25*4 = 3.0
  EXPECT_NEAR(countPenalty<CountClass::EditWord>(in, overrides), 3.0, 1e-12);
}

TEST(CountPenaltyTest, CountCostIsConcaveAndPiecewiseLinear) {
  // EditLine: base=0, countSlope=0.5. Full slope through 4 extra units, half
  // through the next 10, a fifth beyond.
  auto pen = [](int count) {
    return countPenalty<CountClass::EditLine>(CountPenaltyInput{count, 0});
  };
  EXPECT_NEAR(pen(5), 0.5 * 4, 1e-12);
  EXPECT_NEAR(pen(10), 0.5 * (4 + 0.5 * 5), 1e-12);
  EXPECT_NEAR(pen(15), 0.5 * (4 + 0.5 * 10), 1e-12);
  EXPECT_NEAR(pen(40), 0.5 * (4 + 0.5 * 10 + 0.2 * 25), 1e-12);

  double prevMarginal = pen(2) - pen(1);
  for (int count = 3; count <= 60; count++) {
    const double marginal = pen(count) - pen(count - 1);
    EXPECT_LE(marginal, prevMarginal + 1e-12) << "count " << count;
    EXPECT_GT(marginal, 0.0) << "count " << count;
    prevMarginal = marginal;
  }
}
