#pragma once

#include <functional>  // for std::less<>
#include <map>
#include <string>
#include <vector>

#include "KeyboardModel.h"
#include "CharToKeys.h"
#include "SequenceTokenizer.h"
#include "Optimizer/BufferIndex.h"  // for CountableMotionPair, LandingType

// std::less<> enables transparent comparison - allows lookup with string_view
// without allocating a temporary std::string
using MotionToKeys = std::map<std::string, PhysicalKeys, std::less<>>;

// =============================================================================
// Motion Maps
// =============================================================================

// All physical key/action mappings (for tokenizing raw input)
extern const MotionToKeys ACTION_MOTIONS_TO_KEYS;

// All supported vim motions (for parsing/validation)
extern const MotionToKeys ALL_MOTIONS;

// Motions that can be directly explored in optimizer search loop.
// Excludes motions needing special handling (f/F/t/T require target char, ;/, require prior context)
extern const MotionToKeys EXPLORABLE_MOTIONS;

// Single-character to PhysicalKeys mapping (for f/F/t/T motion targets)
extern const CharToKeys CHAR_TO_KEYS;

// COUNT_SEARCHABLE = what motions we want to find best prefix counts for
// Only apply when same line as end
extern const std::vector<CountableMotionPair> COUNT_SEARCHABLE_MOTIONS_LINE;
// No restrictions
extern const std::vector<CountableMotionPair> COUNT_SEARCHABLE_MOTIONS_GLOBAL;

// Combine line + global
extern const std::vector<std::string> COUNT_SEARCHABLE_MOTIONS;

// =============================================================================
// Utilities
// =============================================================================

MotionToKeys getSlicedMotionToKeys(std::vector<std::string> motions);

// Global tokenizer built from the action and motion maps
const SequenceTokenizer& globalTokenizer();
