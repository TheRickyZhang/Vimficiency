#include <gtest/gtest.h>

#include "LuaExports/Common.h"

namespace payload = VF::LuaExports::payload;

TEST(LuaExportsPayloadTest, DecodeLineArrayPreservesTrailingEmptyLine) {
  auto result = payload::decodeLineArray("1:a0:");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0], "a");
  EXPECT_EQ((*result)[1], "");
}
