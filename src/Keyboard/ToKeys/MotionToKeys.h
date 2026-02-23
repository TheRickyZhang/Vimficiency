#pragma once

#include <string>
#include <vector>

#include "CharToKeys.h"
#include "CommandToKeys.h"
#include "SequenceToKeys.h"

// std::less<> enables transparent comparison - allows lookup with string_view
// without allocating a temporary std::string
using MotionToKeys = CommandToKeys;

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

// =============================================================================
// Utilities
// =============================================================================

MotionToKeys getSlicedMotionToKeys(std::vector<std::string> motions);

// Global tokenizer built from the action and motion maps
const SequenceToKeys& globalSequenceToKeys();
