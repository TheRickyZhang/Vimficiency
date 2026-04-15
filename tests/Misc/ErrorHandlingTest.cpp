// tests/Misc/ErrorHandlingTest.cpp
//
// Tests for error handling in motion/edit parsing.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="*ErrorHandling*"

#include <gtest/gtest.h>

#include "Interpreter/MotionInterpreter.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/ToKeys/SequenceToKeys.h"
#include "Keyboard/ToKeys/MotionToKeys.h"

using namespace std;

TEST(ErrorHandlingTest, ParseMotionsRejectsUnknownMotion) {
  auto result = parseMotions("q");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, MotionParseErrorKind::UnknownMotion);
  EXPECT_EQ(result.error().offset, 0u);
}

TEST(ErrorHandlingTest, ParseMotionsRejectsMalformedSpecialKey) {
  auto result = parseMotions("<C-x");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, MotionParseErrorKind::MalformedSpecialKey);
  EXPECT_EQ(result.error().offset, 0u);
}

TEST(ErrorHandlingTest, ParseSequenceRejectsUnknownCharacter) {
  // '!' is not a motion, delete, change, count prefix, or insert command.
  auto result = parseSequence("!");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, SequenceParseErrorKind::UnknownCharacter);
  EXPECT_EQ(result.error().offset, 0u);
}

TEST(ErrorHandlingTest, ParseSequenceRejectsMalformedSpecialKeyInCommandMode) {
  // '<C-x' in command-mode slot: unclosed special key, not a known motion.
  auto result = parseSequence("<C-x");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, SequenceParseErrorKind::MalformedSpecialKey);
  EXPECT_EQ(result.error().offset, 0u);
}

TEST(ErrorHandlingTest, ParseSequenceTolerantOfLiteralAngleInInsertMode) {
  // 'i<hello<Esc>': '<hello' is literal typed text (no closing '>'), so
  // the parser must not error — it's how vim itself treats a bare '<'.
  auto result = parseSequence("i<hello<Esc>");
  ASSERT_TRUE(result.has_value());
}


// Test that SequenceToKeys throws for unknown key sequences
// TEST(ErrorHandlingTest, TokenizerThrowsForUnknownSequence) {
//   const auto& tokenizer = globalSequenceToKeys();
//
//   // Emoji or special chars not in the keyboard model should throw
//   EXPECT_THROW(tokenizer.tokenize("\x01"), runtime_error);  // Control char
// }

TEST(ErrorHandlingTest, TokenizerAcceptsValidSequences) {
  const auto& tokenizer = globalSequenceToKeys();

  // Valid alphanumeric keys
  EXPECT_NO_THROW(tokenizer.tokenize("abc"));
  EXPECT_NO_THROW(tokenizer.tokenize("123"));
  EXPECT_NO_THROW(tokenizer.tokenize("wWbBeE"));
}
