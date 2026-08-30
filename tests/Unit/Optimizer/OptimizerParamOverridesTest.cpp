#include <gtest/gtest.h>

#include <string_view>

#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"

namespace {

OptimizerParamOverrides parseGood(std::string_view encoded) {
  auto parsed = parseOptimizerParamOverrides(encoded);
  EXPECT_TRUE(parsed.has_value())
      << (parsed ? "" : formatOptimizerParamOverrideErrors(parsed.error()));
  if (!parsed) return {};
  return std::move(*parsed);
}

TEST(OptimizerParamOverrides, EmptyInputLeavesParamsUntouched) {
  const auto overrides = parseGood("");
  EXPECT_TRUE(overrides.empty());

  NavOptimizerParams nav;
  const auto navBefore = nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.maxResults, navBefore.maxResults);
  EXPECT_EQ(nav.effortWeight, navBefore.effortWeight);
  EXPECT_EQ(nav.fMotionThreshold, navBefore.fMotionThreshold);
}

TEST(OptimizerParamOverrides, SharedKeysApplyToAllOptimizers) {
  const auto overrides = parseGood(
      "shared:effortWeight=1.5\n"
      "shared:distanceWeight=0.5\n"
      "shared:maxResults=42");

  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_DOUBLE_EQ(nav.effortWeight, 1.5);
  EXPECT_DOUBLE_EQ(nav.distanceWeight, 0.5);
  EXPECT_EQ(nav.maxResults, 42);

  TransformOptimizerParams tx;
  overrides.applyTo(tx);
  EXPECT_DOUBLE_EQ(tx.effortWeight, 1.5);
  EXPECT_DOUBLE_EQ(tx.distanceWeight, 0.5);
  EXPECT_EQ(tx.maxResults, 42);

  CompositionOptimizerParams comp;
  overrides.applyTo(comp);
  EXPECT_DOUBLE_EQ(comp.effortWeight, 1.5);
  EXPECT_DOUBLE_EQ(comp.distanceWeight, 0.5);
  EXPECT_EQ(comp.maxResults, 42);
}

TEST(OptimizerParamOverrides, PerOptimizerScopeWinsOverShared) {
  const auto overrides = parseGood(
      "shared:maxResults=20\n"
      "nav:maxResults=5\n"
      "transform:maxResults=99");

  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.maxResults, 5);

  TransformOptimizerParams tx;
  overrides.applyTo(tx);
  EXPECT_EQ(tx.maxResults, 99);

  // Composition only sees `shared:` since no `composition:maxResults` line.
  CompositionOptimizerParams comp;
  overrides.applyTo(comp);
  EXPECT_EQ(comp.maxResults, 20);
}

TEST(OptimizerParamOverrides, SharedScopeRejectsConcreteKeys) {
  const auto parsed = parseOptimizerParamOverrides(
      "shared:fMotionThreshold=7\n"
      "shared:maxResultsPerEndPos=3\n"
      "shared:minPrefixCount=5\n"
      "shared:maxPrefixCount=12");

  ASSERT_FALSE(parsed.has_value());
  const auto message = formatOptimizerParamOverrideErrors(parsed.error());
  EXPECT_NE(message.find("shared:fMotionThreshold"), std::string::npos);
  EXPECT_NE(message.find("shared:maxResultsPerEndPos"), std::string::npos);
  EXPECT_EQ(message.find("shared:minPrefixCount"), std::string::npos);
  EXPECT_EQ(message.find("shared:maxPrefixCount"), std::string::npos);
}

TEST(OptimizerParamOverrides, ConcreteScopesApplyBaseAndConcreteKeys) {
  const auto overrides = parseGood(
      "nav:minPrefixCount=3\n"
      "nav:fMotionThreshold=7\n"
      "nav:maxResultsPerEndPos=3\n"
      "composition:fMotionThreshold=9\n"
      "transform:maxResultsPerStartPos=4");

  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.minPrefixCount, 3);
  EXPECT_EQ(nav.fMotionThreshold, 7);
  EXPECT_EQ(nav.maxResultsPerEndPos, 3);

  CompositionOptimizerParams comp;
  overrides.applyTo(comp);
  EXPECT_EQ(comp.fMotionThreshold, 9);

  TransformOptimizerParams tx;
  overrides.applyTo(tx);
  EXPECT_EQ(tx.maxResultsPerStartPos, 4);
}

TEST(OptimizerParamOverrides, BoolParsedAsZeroOneOrTrueFalse) {
  // Boolean encoding: the Lua-side `encode_optimizer_overrides` emits
  // 0/1, but the parser also tolerates true/false for human-authored
  // overrides (e.g. via :Vimfy debug commands).
  for (auto encoded : {"nav:useDirectionalPruning=0",
                       "nav:useDirectionalPruning=false"}) {
    const auto overrides = parseGood(encoded);
    NavOptimizerParams nav;
    overrides.applyTo(nav);
    EXPECT_FALSE(nav.useDirectionalPruning) << "encoded: " << encoded;
  }
  for (auto encoded : {"nav:useDirectionalPruning=1",
                       "nav:useDirectionalPruning=true"}) {
    NavOptimizerParams nav;
    nav.useDirectionalPruning = false;  // start from non-default
    parseGood(encoded).applyTo(nav);
    EXPECT_TRUE(nav.useDirectionalPruning) << "encoded: " << encoded;
  }
}

TEST(OptimizerParamOverrides, InvalidLinesRejectPayload) {
  const auto parsed = parseOptimizerParamOverrides(
      "shared:effortWeight=2.5\n"
      "this line has no colon\n"
      "shared:noEqualSign\n"
      "unknown_scope:maxResults=10\n"
      "shared:distanceWeight=bad\n"
      "shared:distanceWeight=0.25");

  ASSERT_FALSE(parsed.has_value());
  const auto message = formatOptimizerParamOverrideErrors(parsed.error());
  EXPECT_NE(message.find("line 2"), std::string::npos);
  EXPECT_NE(message.find("line 3"), std::string::npos);
  EXPECT_NE(message.find("unknown scope"), std::string::npos);
  EXPECT_NE(message.find("invalid value"), std::string::npos);
}

TEST(OptimizerParamOverrides, CompositionSpecificKeyAppliesOnlyToComposition) {
  const auto overrides = parseGood(
      "composition:overshootPenalty=10.0\n"
      "composition:diffAlgorithm=1");

  CompositionOptimizerParams comp;
  overrides.applyTo(comp);
  EXPECT_DOUBLE_EQ(comp.overshootPenalty, 10.0);
  EXPECT_EQ(comp.diffAlgorithm, 1);

  // Nav and Transform never read the composition scope.
  NavOptimizerParams nav;
  overrides.applyTo(nav);
  // Nothing to assert on Nav directly; this is a "doesn't crash, doesn't
  // mis-set" sanity check.
  EXPECT_EQ(nav.maxResults, NavOptimizerParams{}.maxResults);
}

TEST(OptimizerParamOverrides, CountPrefixSettersHonorBaseValidation) {
  // OptimizerParamsBase::validate() asserts the prefix-count fields are in
  // bounds. The override applier calls validate() at the end of applyTo()
  // so callers can't slip through bad values.
  const auto overrides = parseGood(
      "shared:minPrefixCount=5\n"
      "shared:maxPrefixCount=12");
  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.minPrefixCount, 5);
  EXPECT_EQ(nav.maxPrefixCount, 12);
  EXPECT_TRUE(nav.countPrefixesEnabled());
}

}  // namespace
