#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "CharToKeys.h"
#include "Keyboard/PhysicalKeys.h"

// Allow unordered_map<string, ...> to use alternative equivalent types, like string_view
struct StringHash {
  // All of std::string, const char*, std::string_view are convertable to string_view
  using is_transparent = void;
  size_t operator()(const std::string_view sv) const {
    return std::hash<std::string_view>{}(sv);
  }
};

using CommandToKeys = std::unordered_map<std::string, PhysicalKeys, StringHash, std::equal_to<>>;

// =============================================================================
// Combine Utilities
// =============================================================================
// Use reference_wrapper to avoid overhead of copying, while giving concrete
// objects to initializer list

// Merge multiple CommandToKeys maps into one
CommandToKeys combineAll(std::initializer_list<std::reference_wrapper<const CommandToKeys>> maps);

// Convert CommandToKeys (single-char keys only) to CharToKeys
CharToKeys combineAllToCharKeySeq(std::initializer_list<std::reference_wrapper<const CommandToKeys>> maps);

// Extract all keys from multiple maps as a sorted list
std::vector<std::string> combineAllToList(std::initializer_list<std::reference_wrapper<const CommandToKeys>> maps);
