#pragma once

#include <map>
#include <string>
#include <vector>

#include "KeyboardModel.h"
#include "SequenceTokenizer.h"

extern std::map<std::string, std::vector<Key>> actionToKeys;
extern std::map<std::string, std::vector<Key>> motionToKeys;

// Global tokenizer built from the two maps
const SequenceTokenizer& globalTokenizer();
