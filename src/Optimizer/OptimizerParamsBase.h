#pragma once

#include <cassert>

#include "Types/CountPrefixLimits.h"

// X-macro field lists are the single source of truth for the struct decls,
// `with*` setters, knownKeys(), and the override applier in
// OptimizerParamOverrides.cpp. Columns: (type, fieldName, withName, default,
// parser). `parser` is just a token in headers — only the override applier
// resolves it (parseInt/parseDouble/parseBool).

#define OPTIMIZER_BASE_FIELDS(F)                                                       \
    F(int,    maxResults,     withMaxResults,     20,    parseInt)                     \
    F(int,    maxNodesPopped, withMaxNodesPopped, 50000, parseInt)                     \
    F(double, exploreFactor,  withExploreFactor,  2.0,   parseDouble)                  \
    F(double, effortWeight,   withEffortWeight,   1.0,   parseDouble)                  \
    F(double, distanceWeight, withDistanceWeight, 1.0,   parseDouble)                  \
    /* minPrefixCount > maxPrefixCount disables count-prefixed exploration             \
       while leaving unprefixed exploration intact. */                                 \
    F(int,    minPrefixCount, withMinCountRepeat, 4,     parseInt)                     \
    F(int,    maxPrefixCount, withMaxCountRepeat, 16,    parseInt)

// Shared between Nav and Composition. Not lifted to base — Transform doesn't
// enumerate motions, so these would be dead there.
#define MOTION_CLASS_FIELDS(F)                                                          \
    F(int,  fMotionThreshold,      withFMotionThreshold,    3,    parseInt)             \
    F(bool, useDirectionalPruning, withDirectionalPruning,  true, parseBool)

#define VF_PARAMS_DECLARE_OWN_FIELD(type, name, withName, def, parser)  \
    type name = def;

// Derived structs invoke this after `#define VF_PARAMS_SELF <derived>` so the
// setter chains return the derived type.
#define VF_PARAMS_WITH_SETTER(type, name, withName, def, parser)        \
    VF_PARAMS_SELF& withName(type v) { name = v; return *this; }

struct OptimizerParamsBase {
  OPTIMIZER_BASE_FIELDS(VF_PARAMS_DECLARE_OWN_FIELD)

  // Called only from OptimizerParamOverrides::applyTo() — the trust boundary
  // for external Lua-config input. Direct in-tree construction uses literals
  // and is trusted, so optimizer entry points don't validate.
  void validate() const {
    assert(minPrefixCount >= CountPrefixLimits::MIN_PREFIX_COUNT);
    assert(maxPrefixCount >= 0 && maxPrefixCount <= CountPrefixLimits::MAX_PREFIX_COUNT);
  }

  bool countPrefixesEnabled() const { return minPrefixCount <= maxPrefixCount; }
  void disableCountPrefixes() {
    minPrefixCount = CountPrefixLimits::MIN_PREFIX_COUNT;
    maxPrefixCount = 0;
  }
};
