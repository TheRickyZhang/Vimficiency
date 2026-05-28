// Tests to verify no hash collisions in edit command parsing.

#include <gtest/gtest.h>
#include <string_view>

#include "Interpreter/EditHash.h"

using Edit::hash;
using Edit::HASH_MULTIPLIER;

TEST(HashCollisionTest, MultiplierExceedsMaxAscii) {
  constexpr unsigned char MAX_ASCII = 126;  // '~'
  EXPECT_GT(HASH_MULTIPLIER, MAX_ASCII)
      << "Hash multiplier must be > 126 to guarantee collision-free hashing";
}

TEST(HashCollisionTest, HashFunctionIsConsistent) {
  constexpr size_t compileTime = hash("test");
  size_t runTime = hash(std::string_view("test"));
  EXPECT_EQ(compileTime, runTime);
}

TEST(HashCollisionTest, HashMatchesBaseConversion) {
  // hash("ab") = 'a'*131 + 'b' = 97*131 + 98 = 12805
  constexpr size_t expected = 97 * 131 + 98;
  EXPECT_EQ(hash("ab"), expected);
}

TEST(HashCollisionTest, EmptyStringHashIsZero) {
  EXPECT_EQ(hash(""), 0);
}
