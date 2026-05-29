#include <gtest/gtest.h>

#include "LuaExports/Common.h"
#include "Types/Lines.h"

namespace payload = VF::LuaExports::payload;

TEST(LuaExportsPayloadTest, DecodeLineArrayPreservesTrailingEmptyLine) {
  auto result = payload::decodeLineArray("1:a0:");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0], "a");
  EXPECT_EQ((*result)[1], "");
}

// Regression: the analyze FFI boundary clamps captured cursors into a valid
// normal-mode target. Neovim can report an insert-mode / 'virtualedit' cursor
// one past end-of-line, which the optimizer's contains() invariant rejects.
TEST(LuaExportsPayloadTest, ClampMapsCursorIntoNormalModeTarget) {
  Lines lines = {"abc", "", "de"};

  auto pastEol = lines.clamp(CursorPos(0, 3));   // insert pos at EOL
  EXPECT_EQ(pastEol.line, 0);
  EXPECT_EQ(pastEol.col, 2);

  auto onEmpty = lines.clamp(CursorPos(1, 5));   // empty line -> col 0
  EXPECT_EQ(onEmpty.line, 1);
  EXPECT_EQ(onEmpty.col, 0);

  auto pastEnd = lines.clamp(CursorPos(9, 9));   // row past last -> last line/char
  EXPECT_EQ(pastEnd.line, 2);
  EXPECT_EQ(pastEnd.col, 1);

  EXPECT_TRUE(lines.contains(lines.clamp(CursorPos(0, 1))));   // already valid
  EXPECT_TRUE(lines.contains(lines.clamp(CursorPos(-1, -1)))); // negative
}
