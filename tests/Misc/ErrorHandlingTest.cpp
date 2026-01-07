#include <gtest/gtest.h>
#include <stdexcept>

#include "Editor/Motion.h"
#include "Keyboard/SequenceTokenizer.h"
#include "Keyboard/MotionToKeys.h"

using namespace std;

// Test that parseMotions throws for unknown motions
TEST(ErrorHandlingTest, ParseMotionsThrowsForUnknownMotion) {
  // "q" is not a valid motion
  EXPECT_THROW(parseMotions("q"), runtime_error);

  // "Z" is not a valid motion
  EXPECT_THROW(parseMotions("Z"), runtime_error);

  // "x" is not a valid motion (delete char, not implemented)
  EXPECT_THROW(parseMotions("x"), runtime_error);
}

TEST(ErrorHandlingTest, ParseMotionsThrowsForUnknownSpecialKey) {
  // Unknown special key format
  EXPECT_THROW(parseMotions("<C-x>"), runtime_error);

  // Malformed special key (no closing >)
  EXPECT_THROW(parseMotions("<C-d"), runtime_error);
}

TEST(ErrorHandlingTest, ParseMotionsAcceptsValidMotions) {
  // Should not throw for valid motions
  EXPECT_NO_THROW(parseMotions("wWbBeE"));
  EXPECT_NO_THROW(parseMotions("hjkl"));
  EXPECT_NO_THROW(parseMotions("gg"));
  EXPECT_NO_THROW(parseMotions("G"));
  EXPECT_NO_THROW(parseMotions("3w5j"));
  EXPECT_NO_THROW(parseMotions("<C-d>"));
  EXPECT_NO_THROW(parseMotions("<C-u>"));
  EXPECT_NO_THROW(parseMotions("<C-f>"));
  EXPECT_NO_THROW(parseMotions("<C-b>"));
  EXPECT_NO_THROW(parseMotions("fa"));
  EXPECT_NO_THROW(parseMotions("Fa;,"));
}

// Test that SequenceTokenizer throws for unknown key sequences
TEST(ErrorHandlingTest, TokenizerThrowsForUnknownSequence) {
  const auto& tokenizer = globalTokenizer();

  // Emoji or special chars not in the keyboard model should throw
  EXPECT_THROW(tokenizer.tokenize("\x01"), runtime_error);  // Control char
}

TEST(ErrorHandlingTest, TokenizerAcceptsValidSequences) {
  const auto& tokenizer = globalTokenizer();

  // Valid alphanumeric keys
  EXPECT_NO_THROW(tokenizer.tokenize("abc"));
  EXPECT_NO_THROW(tokenizer.tokenize("123"));
  EXPECT_NO_THROW(tokenizer.tokenize("wWbBeE"));
}
