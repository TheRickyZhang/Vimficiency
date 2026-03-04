#pragma once

namespace CountPrefixLimits {
// Global preference: there is absolutely no point in setting the minimum less than 2
inline constexpr int MIN_PREFIX_COUNT = 2;

// Implementation necessity: for static number / key / effort association, we must have a max, and 99 is reasonable
inline constexpr int MAX_PREFIX_COUNT = 99;
}  // namespace CountPrefixLimits
