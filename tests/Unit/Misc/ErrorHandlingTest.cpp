// tests/Unit/Misc/ErrorHandlingTest.cpp
//
// Tests for error handling in motion/edit parsing.
//
// Run: ./build/tests/vimfy_unit_tests --gtest_filter="*ErrorHandling*"

#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Interpreter/SequenceFormatting.h"
#include "Interpreter/SequenceParser.h"
#include "Keyboard/ToKeys/SequenceToKeys.h"
#include "Keyboard/ToKeys/MovementToKeys.h"

using namespace std;

TEST(ErrorHandlingTest, ParseMotionsRejectsUnknownMotion) {
  auto result = parseMovements("q");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, MovementParseErrorKind::UnknownMotion);
  EXPECT_EQ(result.error().offset, 0u);
}

TEST(ErrorHandlingTest, ParseMotionsRejectsMalformedSpecialKey) {
  auto result = parseMovements("<C-x");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().kind, MovementParseErrorKind::MalformedSpecialKey);
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

TEST(ErrorHandlingTest, ParseSequenceKeepsDigitLeadingTypedTextAfterChange) {
  auto result = parseSequence("s3<Esc>");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ((*result)[0].token, "s");
  EXPECT_EQ((*result)[0].kind, TokenKind::Change);
  EXPECT_EQ((*result)[1].token, "3");
  EXPECT_EQ((*result)[1].kind, TokenKind::TypedText);
  EXPECT_EQ((*result)[2].token, "<Esc>");
  EXPECT_EQ((*result)[2].kind, TokenKind::Escape);
}

TEST(ErrorHandlingTest, ParseSequenceKeepsDigitLeadingTypedTextInInsertMode) {
  auto result = parseSequence("i123<Esc>");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ((*result)[0].token, "i");
  EXPECT_EQ((*result)[0].kind, TokenKind::Change);
  EXPECT_EQ((*result)[1].token, "123");
  EXPECT_EQ((*result)[1].kind, TokenKind::TypedText);
  EXPECT_EQ((*result)[2].token, "<Esc>");
  EXPECT_EQ((*result)[2].kind, TokenKind::Escape);
}

TEST(ErrorHandlingTest, FormatSequenceDisplaysDigitLeadingTypedText) {
  EXPECT_EQ(formatSequenceForDisplay("s3<Esc>"), "s 3 <Esc>");
  EXPECT_EQ(formatSequenceForDisplay("i123<Esc>"), "i 123 <Esc>");
}

TEST(ErrorHandlingTest, ParseSequenceAcceptsCanonicalLiteralLtTargets) {
  auto motion = parseSequence("f<lt>l");
  ASSERT_TRUE(motion.has_value());
  ASSERT_EQ(motion->size(), 2u);
  EXPECT_EQ((*motion)[0].token, "f<lt>");
  EXPECT_EQ((*motion)[1].token, "l");

  auto replace = parseSequence("r<lt>");
  ASSERT_TRUE(replace.has_value());
  ASSERT_EQ(replace->size(), 1u);
  EXPECT_EQ((*replace)[0].token, "r<lt>");
}

TEST(ErrorHandlingTest, ParseSequenceSplitsCharFindRepeats) {
  auto result = parseSequence("fa2;");
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0].token, "fa");
  EXPECT_EQ((*result)[1].token, "2;");
}

TEST(ErrorHandlingTest, ParseMovementsAcceptsCanonicalFindTargets) {
  auto space = parseMovements("f<Space>l");
  ASSERT_TRUE(space.has_value());
  ASSERT_EQ(space->size(), 2u);
  EXPECT_EQ((*space)[0].motion, "f<Space>");
  EXPECT_EQ((*space)[1].motion, "l");

  auto literalLt = parseMovements("f<lt>l");
  ASSERT_TRUE(literalLt.has_value());
  ASSERT_EQ(literalLt->size(), 2u);
  EXPECT_EQ((*literalLt)[0].motion, "f<lt>");
  EXPECT_EQ((*literalLt)[1].motion, "l");
}

TEST(ErrorHandlingTest, ParseMovementsSplitsCharFindRepeats) {
  auto repeated = parseMovements("2fa3;,");
  ASSERT_TRUE(repeated.has_value());
  ASSERT_EQ(repeated->size(), 3u);
  EXPECT_EQ((*repeated)[0].motion, "fa");
  EXPECT_EQ((*repeated)[0].effectiveCount(), 2u);
  EXPECT_EQ((*repeated)[1].motion, ";");
  EXPECT_EQ((*repeated)[1].effectiveCount(), 3u);
  EXPECT_EQ((*repeated)[2].motion, ",");

  auto semicolonTarget = parseMovements("f;;");
  ASSERT_TRUE(semicolonTarget.has_value());
  ASSERT_EQ(semicolonTarget->size(), 2u);
  EXPECT_EQ((*semicolonTarget)[0].motion, "f;");
  EXPECT_EQ((*semicolonTarget)[1].motion, ";");
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
  EXPECT_EQ(tokenizer.tokenize("<lt>").size(), CHAR_TO_KEYS.at('<').size());
}
