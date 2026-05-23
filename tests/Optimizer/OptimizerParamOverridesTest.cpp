#include <gtest/gtest.h>

#include "Optimizer/CompositionOptimizer/CompositionOptimizerParams.h"
#include "Optimizer/NavOptimizer/NavOptimizerParams.h"
#include "Optimizer/OptimizerParamOverrides.h"
#include "Optimizer/TransformOptimizer/TransformOptimizerParams.h"

namespace {

TEST(OptimizerParamOverrides, EmptyInputLeavesParamsUntouched) {
  const auto overrides = OptimizerParamOverrides::parse("");
  EXPECT_TRUE(overrides.empty());

  NavOptimizerParams nav;
  const auto navBefore = nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.maxResults, navBefore.maxResults);
  EXPECT_EQ(nav.effortWeight, navBefore.effortWeight);
  EXPECT_EQ(nav.fMotionThreshold, navBefore.fMotionThreshold);
}

TEST(OptimizerParamOverrides, SharedKeysApplyToAllOptimizers) {
  const auto overrides = OptimizerParamOverrides::parse(
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
  const auto overrides = OptimizerParamOverrides::parse(
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

TEST(OptimizerParamOverrides, SharedScopeAppliesOnlyBaseKeys) {
  // `shared:` means OptimizerParamsBase only. Fields that exist on multiple
  // concrete optimizers still need explicit concrete scopes.
  const auto overrides = OptimizerParamOverrides::parse(
      "shared:fMotionThreshold=7\n"
      "shared:maxResultsPerEndPos=3\n"
      "shared:minPrefixCount=5\n"
      "shared:maxPrefixCount=12");

  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.fMotionThreshold, NavOptimizerParams{}.fMotionThreshold);
  EXPECT_EQ(nav.maxResultsPerEndPos, NavOptimizerParams{}.maxResultsPerEndPos);
  EXPECT_EQ(nav.minPrefixCount, 5);
  EXPECT_EQ(nav.maxPrefixCount, 12);

  CompositionOptimizerParams comp;
  overrides.applyTo(comp);
  EXPECT_EQ(comp.fMotionThreshold, CompositionOptimizerParams{}.fMotionThreshold);
  EXPECT_EQ(comp.minPrefixCount, 5);
  EXPECT_EQ(comp.maxPrefixCount, 12);

  TransformOptimizerParams tx;
  overrides.applyTo(tx);
  EXPECT_EQ(tx.minPrefixCount, 5);
  EXPECT_EQ(tx.maxPrefixCount, 12);
}

TEST(OptimizerParamOverrides, ConcreteScopesApplyBaseAndConcreteKeys) {
  const auto overrides = OptimizerParamOverrides::parse(
      "nav:minPrefixCount=3\n"
      "nav:fMotionThreshold=7\n"
      "nav:maxResultsPerEndPos=3\n"
      "composition:fMotionThreshold=9\n"
      "transform:maxResultsPerStartPos=4\n"
      "transform:fMotionThreshold=11");

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
    const auto overrides = OptimizerParamOverrides::parse(encoded);
    NavOptimizerParams nav;
    overrides.applyTo(nav);
    EXPECT_FALSE(nav.useDirectionalPruning) << "encoded: " << encoded;
  }
  for (auto encoded : {"nav:useDirectionalPruning=1",
                       "nav:useDirectionalPruning=true"}) {
    NavOptimizerParams nav;
    nav.useDirectionalPruning = false;  // start from non-default
    OptimizerParamOverrides::parse(encoded).applyTo(nav);
    EXPECT_TRUE(nav.useDirectionalPruning) << "encoded: " << encoded;
  }
}

TEST(OptimizerParamOverrides, MalformedLinesAreSkipped) {
  // Parser is tolerant — wire-format drift shouldn't crash. Valid lines
  // surrounding malformed ones still apply.
  const auto overrides = OptimizerParamOverrides::parse(
      "shared:effortWeight=2.5\n"
      "this line has no colon\n"
      "shared:noEqualSign\n"
      "unknown_scope:maxResults=10\n"
      "shared:distanceWeight=0.25");

  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_DOUBLE_EQ(nav.effortWeight, 2.5);
  EXPECT_DOUBLE_EQ(nav.distanceWeight, 0.25);
  // The malformed lines didn't cause anything to land; `unknown_scope`
  // is dropped before apply.
  EXPECT_EQ(nav.maxResults, NavOptimizerParams{}.maxResults);
}

TEST(OptimizerParamOverrides, CompositionSpecificKeyAppliesOnlyToComposition) {
  const auto overrides = OptimizerParamOverrides::parse(
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
  const auto overrides = OptimizerParamOverrides::parse(
      "shared:minPrefixCount=5\n"
      "shared:maxPrefixCount=12");
  NavOptimizerParams nav;
  overrides.applyTo(nav);
  EXPECT_EQ(nav.minPrefixCount, 5);
  EXPECT_EQ(nav.maxPrefixCount, 12);
  EXPECT_TRUE(nav.countPrefixesEnabled());
}

}  // namespace
