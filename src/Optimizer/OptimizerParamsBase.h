#pragma once

#include <cassert>

#include "Types/CountPrefixLimits.h"

// =============================================================================
// OptimizerParamsBase - Shared parameters for all optimizer types
// =============================================================================
// Base struct containing search parameters common to Motion, Edit, and
// Composition optimizers. Derived structs add optimizer-specific fields.

struct OptimizerParamsBase {
  // LIKELY OVERRIDE CANDIDATE: among the shared base fields, this is
  // the most common one users will want to differ per optimizer
  // We could override per optimizer params, but having a consistent value is just easiest
  int maxResults = 20;

  // Hard search budget: every priority-queue pop counts, including stale
  // or otherwise discarded states. This is the primary runtime guard.
  int maxNodesPopped = 50000;

  // Search stops when effort > baseEffort * exploreFactor
  // baseEffort is typically user's sequence effort or cheapest result found
  double exploreFactor = 2.0;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // (default A*): effortWeight=1.0, distanceWeight=1.0
  // (Dijkstra):   effortWeight=1.0, distanceWeight=0.0
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  // Minimum and maximum numbered command counts to search (e.g. 5 in 5w).
  // An empty range (minPrefixCount > maxPrefixCount) explicitly disables
  // count-prefixed exploration while leaving unprefixed exploration intact.
  int minPrefixCount = 4;
  int maxPrefixCount = 16;

  void setMinCountRepeat(int v) {
    assert(v >= CountPrefixLimits::MIN_PREFIX_COUNT);
    minPrefixCount = v;
  }
  void setMaxCountRepeat(int v) {
    assert(v >= 0 && v <= CountPrefixLimits::MAX_PREFIX_COUNT);
    maxPrefixCount = v;
  }

  // Currently unused, but possible to toggle
  bool countPrefixesEnabled() const { return minPrefixCount <= maxPrefixCount; }
  void disableCountPrefixes() {
    minPrefixCount = CountPrefixLimits::MIN_PREFIX_COUNT;
    maxPrefixCount = 0;
  }
};
