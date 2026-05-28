#pragma once

#include <cassert>
#include <cstddef>
#include <string_view>

namespace Edit {

// Compile-time string hash for switch statements. M=131 (prime > 126, the max
// ASCII value used) guarantees collision-free hashing for inputs up to ~10
// chars (overflows 64 bits beyond that).
inline constexpr std::size_t HASH_MULTIPLIER = 131;

constexpr std::size_t hash(std::string_view s) {
  assert(s.size() <= 10 && "hash() only collision-free for strings <= 10 chars");
  std::size_t h = 0;
  for (char c : s) h = h * HASH_MULTIPLIER + static_cast<unsigned char>(c);
  return h;
}

}  // namespace Edit
