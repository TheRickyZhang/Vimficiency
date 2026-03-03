#pragma once

#include <algorithm>
#include <cassert>

#include "Keyboard/ToKeys/CountToKeys.h"

// =============================================================================
// OptimizerParamsBase - Shared parameters for all optimizer types
// =============================================================================
// Base struct containing search parameters common to Motion, Edit, and
// Composition optimizers. Derived structs add optimizer-specific fields.

struct OptimizerParamsBase {
  // Maximum results (paths) to find
  int maxResults = 10;

  // Hard search budget: every priority-queue pop counts, including stale
  // or otherwise discarded states. This is the primary runtime guard.
  int maxTotalPops = 50000;

  // Search stops when effort > baseEffort * exploreFactor
  // baseEffort is typically user's sequence effort or cheapest result found
  double exploreFactor = 2.0;

  // A* priority weights: priority = effortWeight * effort + distanceWeight * heuristic
  // (default A*): effortWeight=1.0, distanceWeight=1.0
  // (Dijkstra):   effortWeight=1.0, distanceWeight=0.0
  double effortWeight = 1.0;
  double distanceWeight = 1.0;

  // Note these might not really belong here, bgut
  int linePaddingAbove = 2;
  int linePaddingBelow = 2;

  // Minimum count before emitting counted motions/edits (e.g., 4 means 3w→www, 4w→4w).
  // Positions reachable via count < threshold are still found by step-by-step A* exploration.
  int minPrefixCount = 4;
  // Upper bound for emitting counted motions/edits.
  // withMaxCountRepeat enforces this to be <= MAX_PREFIX_COUNT.
  int maxPrefixCount = MAX_PREFIX_COUNT;

  void setMinCountRepeat(int v) {
    minPrefixCount = std::max(0, v);
    if (minPrefixCount > maxPrefixCount) {
      minPrefixCount = maxPrefixCount;
    }
  }

  void setMaxCountRepeat(int v) {
    assert(v >= 0 && v <= MAX_PREFIX_COUNT);
    maxPrefixCount = v;
    if (minPrefixCount > maxPrefixCount) {
      minPrefixCount = maxPrefixCount;
    }
  }

  void normalizeCountRepeatBounds() {
    if (minPrefixCount < 0) {
      minPrefixCount = 0;
    }
    if (minPrefixCount > maxPrefixCount) {
      minPrefixCount = maxPrefixCount;
    }
  }

  // Debug: collect explored states in SearchStats (expensive, for debugging only)
  bool trackExploredStates = false;
};
