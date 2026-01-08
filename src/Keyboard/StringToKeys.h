#pragma once
#include <string_view>
#include <unordered_map>
#include "KeyboardModel.h"

// Allow unordered_map<string, ...> to use alternative equivalent types, like string_view
struct StringHash {
  // All of std::string, const char*, std::string_view are convertable to string_view
  using is_transparent = void;
  size_t operator()(const std::string_view sv) const {
    return std::hash<std::string_view>{}(sv);
  }
};

using StringToKeys = std::unordered_map<std::string, PhysicalKeys, StringHash, std::equal_to<>>;
