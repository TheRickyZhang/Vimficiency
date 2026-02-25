// tests/Misc/ErrorHandlingTest.cpp
//
// Tests for error handling in motion/edit parsing.
//
// Run: ./build/tests/vimficiency_tests --gtest_filter="*ErrorHandling*"

#include <gtest/gtest.h>
#include <stdexcept>

#include "Interpreter/EditInterpreter.h"
#include "Interpreter/MotionInterpreter.h"
#include "Keyboard/ToKeys/SequenceToKeys.h"
#include "Keyboard/ToKeys/MotionToKeys.h"

using namespace std;

TEST(ErrorHandlingTest, ParseMotionsRejectsUnknownMotions) {
  EXPECT_THROW(parseMotions("q"), runtime_error);
  EXPECT_THROW(parseMotions("Z"), runtime_error);
  EXPECT_THROW(parseMotions("x"), runtime_error);
}

TEST(ErrorHandlingTest, ParseMotionsRejectsMalformedSpecialKeys) {
  EXPECT_THROW(parseMotions("<C-d"), runtime_error);
  EXPECT_THROW(parseMotions("<bad>"), runtime_error);
}

TEST(ErrorHandlingTest, ParseEditsRejectsMalformedSpecialKeys) {
  EXPECT_THROW(Edit::parseEdits("<Esc"), runtime_error);
}

TEST(ErrorHandlingTest, TokenizerRejectsUnknownSequences) {
  const auto& tokenizer = globalSequenceToKeys();

  EXPECT_THROW(tokenizer.tokenize("\x01"), runtime_error);
  EXPECT_THROW(tokenizer.tokenize("\xF0\x9F\x98\x80"), runtime_error);
}

TEST(ErrorHandlingTest, TokenizerAcceptsValidSequences) {
  const auto& tokenizer = globalSequenceToKeys();

  // Valid alphanumeric keys
  EXPECT_NO_THROW(tokenizer.tokenize("abc"));
  EXPECT_NO_THROW(tokenizer.tokenize("123"));
  EXPECT_NO_THROW(tokenizer.tokenize("wWbBeE"));
}
