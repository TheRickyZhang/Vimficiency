#pragma once

#include "EditToKeys.h"
#include "MotionToKeysPrimitives.h"  // For combineAll (same underlying type)

#include <functional>
#include <initializer_list>
#include <vector>

// combineAll is provided by MotionToKeysPrimitives.h (EditToKeys == MotionToKeys == StringToKeys)

// Generate all combinations from multiple character slots
// Example: makeCombinations({"d", "c"}, {"i", "a"}, {"w", "W"})
//   -> {"diw", "diW", "daw", "daW", "ciw", "ciW", "caw", "caW"}
// Each string in the input can be multi-char (e.g., "ge" for a single motion)
EditToKeys makeCombinations(std::initializer_list<std::vector<std::string>> slots);
