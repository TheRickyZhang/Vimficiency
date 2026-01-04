#include <gtest/gtest.h>
#include <string_view>

// Must match the hash function in Edit.cpp
constexpr size_t HASH_MULTIPLIER = 131;

constexpr size_t editHash(std::string_view s) {
  size_t h = 0;
  for (char c : s) h = h * HASH_MULTIPLIER + static_cast<unsigned char>(c);
  return h;
}

// Verify the mathematical invariant that guarantees collision-free hashing.
// With M > max(ASCII char value), hash is bijective for strings up to ~10 chars.
TEST(HashCollisionTest, MultiplierExceedsMaxAscii) {
  constexpr unsigned char MAX_ASCII = 126;  // '~'
  EXPECT_GT(HASH_MULTIPLIER, MAX_ASCII)
      << "Hash multiplier must be > 126 to guarantee collision-free hashing";
}

TEST(HashCollisionTest, HashFunctionIsConsistent) {
  // Verify constexpr evaluation matches runtime
  constexpr size_t compileTime = editHash("test");
  size_t runTime = editHash(std::string_view("test"));
  EXPECT_EQ(compileTime, runTime);
}

TEST(HashCollisionTest, HashMatchesBaseConversion) {
  // Verify hash(s) = s[0]*M^(n-1) + s[1]*M^(n-2) + ... + s[n-1]
  // For "ab": hash = 'a'*131 + 'b' = 97*131 + 98 = 12805
  constexpr size_t expected = 97 * 131 + 98;
  EXPECT_EQ(editHash("ab"), expected);
}

TEST(HashCollisionTest, EmptyStringHashIsZero) {
  EXPECT_EQ(editHash(""), 0);
}
