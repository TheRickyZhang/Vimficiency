#pragma once

#include <map>
#include <string>
#include <vector>

#include "KeyboardModel.h"
#include "SequenceTokenizer.h"

using MotionToKeys = std::map<std::string, KeySequence>;

extern const MotionToKeys ACTION_MOTIONS_TO_KEYS;
extern const MotionToKeys ALL_MOTIONS_TO_KEYS;

MotionToKeys getSlicedMotionToKeys(std::vector<std::string> motions);

// Global tokenizer built from the two maps
const SequenceTokenizer& globalTokenizer();
