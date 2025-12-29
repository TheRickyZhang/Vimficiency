#pragma once

#include <map>
#include <string>
#include <vector>

#include "KeyboardModel.h"
#include "SequenceTokenizer.h"

using MotionToKeys = std::map<std::string, KeySequence>;
using CharToKeys = std::map<char, KeySequence>;

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

// Single-character to KeySequence mapping (for f/F/t/T motion targets)
extern const CharToKeys CHAR_TO_KEYS;

// =============================================================================
// Utilities
// =============================================================================

MotionToKeys getSlicedMotionToKeys(std::vector<std::string> motions);

// Global tokenizer built from the action and motion maps
const SequenceTokenizer& globalTokenizer();
