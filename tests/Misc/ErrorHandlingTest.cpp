// tests/Misc/ErrorHandlingTest.cpp
//
// Tests for error handling in motion/edit parsing.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="*ErrorHandling*"

#include <gtest/gtest.h>
#include <stdexcept>

#include "Keyboard/SequenceTokenizer.h"
#include "Keyboard/MotionToKeys.h"

using namespace std;

// Test that parseMotions throws for unknown motions
// TEST(ErrorHandlingTest, ParseMotionsThrowsForUnknownMotion) {
//   EXPECT_THROW(parseMotions("q"), runtime_error);
//   EXPECT_THROW(parseMotions("Z"), runtime_error);
//   EXPECT_THROW(parseMotions("x"), runtime_error);
// }


// Test that SequenceTokenizer throws for unknown key sequences
// TEST(ErrorHandlingTest, TokenizerThrowsForUnknownSequence) {
//   const auto& tokenizer = globalTokenizer();
//
//   // Emoji or special chars not in the keyboard model should throw
//   EXPECT_THROW(tokenizer.tokenize("\x01"), runtime_error);  // Control char
// }

TEST(ErrorHandlingTest, TokenizerAcceptsValidSequences) {
  const auto& tokenizer = globalTokenizer();

  // Valid alphanumeric keys
  EXPECT_NO_THROW(tokenizer.tokenize("abc"));
  EXPECT_NO_THROW(tokenizer.tokenize("123"));
  EXPECT_NO_THROW(tokenizer.tokenize("wWbBeE"));
}
