#pragma once

#include "Editor/Position.h"
#include "Utils/Lines.h"
#include "VimCore/EdgeType.h"

// Alternate implementations for A/B benchmarking.
// These are intentionally NOT optimized versions to compare against
// the production templated implementations.

namespace Alternate {

// Runtime-dispatch version of motionWordCore (no templates).
// Uses if/switch on forward and edgeType parameters.
Position motionWordCoreRuntime(Position pos,
                               const Lines& lines,
                               bool forward,
                               EdgeType edgeType,
                               bool big,
                               bool skipCurrent,
                               bool lineBounded = false);

// Runtime-dispatch version of motionWordEndpoint.
Position motionWordEndpointRuntime(Position cursor,
                                   const Lines& lines,
                                   bool forward,
                                   EdgeType edgeType,
                                   bool big,
                                   bool skipCurrent,
                                   int boundaryOffset,
                                   bool hasLinesOutside,
                                   bool lineBounded = false);

} // namespace Alternate
