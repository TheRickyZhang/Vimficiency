#pragma once

#include <string_view>

class PhysicalKeys;

// Hard ceiling for counted prefixes for static array size
// Runtime count exploration should be <= this
inline constexpr int MAX_PREFIX_COUNT = 99;

namespace CountToKeys {
// Returns prebuilt key prefix for a count in [0, MAX_PREFIX_COUNT].
const PhysicalKeys& keysForCount(int count);

// Not technically to keys, but used adjacently
// Note: count 0 maps to empty string_view
std::string_view textForCount(int count);
}  // namespace CountToKeys
