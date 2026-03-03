#pragma once

#include <string_view>

#include "Types/CountPrefixLimits.h"

class PhysicalKeys;

namespace CountToKeys {
// Returns prebuilt key prefix for a count in [0, CountPrefixLimits::MAX_PREFIX_COUNT].
const PhysicalKeys& keysForCount(int count);

// Not technically to keys, but used adjacently
// Note: count 0 maps to empty string_view
std::string_view textForCount(int count);
}  // namespace CountToKeys
