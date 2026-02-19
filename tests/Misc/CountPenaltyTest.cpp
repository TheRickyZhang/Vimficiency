#include <gtest/gtest.h>

#include "Optimizer/CountPenalty.h"

TEST(CountPenaltyTest, CountOneOrLessHasNoPenalty) {
  CountPenaltyInput in;
  in.span = 5;

  in.count = 0;
  EXPECT_DOUBLE_EQ(countPenalty<CountClass::MotionWord>(in), 0.0);

  in.count = 1;
  EXPECT_DOUBLE_EQ(countPenalty<CountClass::EditLine>(in), 0.0);
}

TEST(CountPenaltyTest, DefaultSpecUsesBaseCountAndSpan) {
  CountPenaltyInput in;
  in.count = 4;
  in.span = 4;

  // MotionWord default: base=1.0, countSlope=0.5, spanSlope=0.1
  // penalty = 1.0 + 0.5*(4-1) + 0.1*4 = 2.9
  EXPECT_NEAR(countPenalty<CountClass::MotionWord>(in), 2.9, 1e-12);
}

TEST(CountPenaltyTest, NegativeSpanIsClampedToZero) {
  CountPenaltyInput in;
  in.count = 3;
  in.span = -20;

  // MotionWord default with span clamped to 0:
  // 1.0 + 0.5*(3-1) + 0.1*0 = 2.0
  EXPECT_NEAR(countPenalty<CountClass::MotionWord>(in), 2.0, 1e-12);
}

TEST(CountPenaltyTest, OverridesAffectOnlySelectedClass) {
  CountPenaltyOverrideTable overrides{};

  PartialCountPenaltyParams wordOverride;
  wordOverride.base = 3.0;
  wordOverride.countSlope = 2.0;
  wordOverride.spanSlope = 0.0;
  overrides[toIndex(CountClass::MotionWord)] = wordOverride;

  CountPenaltyInput in;
  in.count = 3;
  in.span = 10;

  // Overridden MotionWord: 3.0 + 2.0*(3-1) + 0*10 = 7.0
  EXPECT_NEAR(countPenalty<CountClass::MotionWord>(in, overrides), 7.0, 1e-12);

  // MotionSentence remains default: 1.0 + 0.5*(3-1) + 0.1*10 = 3.0
  EXPECT_NEAR(countPenalty<CountClass::MotionSentence>(in, overrides), 3.0, 1e-12);
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
