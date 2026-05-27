#pragma once

#include <optional>

#include "TransformState.h"
#include "VimCore/VimEditUtils.h"

// Protocol types for GoalHandler — shared between the dispatcher and both
// goal handler implementations (DeletionGoalHandler, ChangeGoalHandler).

// on*Goal returns 1-2 EditStates for the dispatcher to emit.
struct GoalStates {
  TransformState primary;
  std::optional<TransformState> dotVariant;
};

struct ResolvedEditAction {
  VimCore::ResolvedDeleteRange deleteEffect;
  std::optional<VimCore::ResolvedDeleteRange> changeEffect;
  bool deleteEffectValid = true;
  bool changeEffectValid = true;

  explicit ResolvedEditAction(VimCore::ResolvedDeleteRange deleteEffect)
      : deleteEffect(deleteEffect), changeEffect(std::nullopt) {}

  ResolvedEditAction(VimCore::ResolvedDeleteRange deleteEffect,
                     VimCore::ResolvedDeleteRange changeEffect)
      : deleteEffect(deleteEffect), changeEffect(changeEffect) {}

  ResolvedEditAction(VimCore::ResolvedDeleteRange deleteEffect,
                     bool deleteEffectValid)
      : deleteEffect(deleteEffect),
        changeEffect(std::nullopt),
        deleteEffectValid(deleteEffectValid),
        changeEffectValid(deleteEffectValid) {}

  ResolvedEditAction(VimCore::ResolvedDeleteRange deleteEffect,
                     VimCore::ResolvedDeleteRange changeEffect,
                     bool deleteEffectValid,
                     bool changeEffectValid)
      : deleteEffect(deleteEffect),
        changeEffect(changeEffect),
        deleteEffectValid(deleteEffectValid),
        changeEffectValid(changeEffectValid) {}

  const VimCore::ResolvedDeleteRange& effectForChange() const {
    return changeEffect ? *changeEffect : deleteEffect;
  }
};

// tryUseSuffixCache returns hit status and whether the start was capped.
struct SuffixCacheResult {
  bool hit = false;
  bool startCapped = false;
};
