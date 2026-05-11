#include <gtest/gtest.h>

#include "LuaExports/Common.h"

#include <string>

namespace payload = VF::LuaExports::payload;

TEST(LuaExportsPayloadTest, DecodeLineArrayPreservesTrailingEmptyLine) {
  auto result = payload::decodeLineArray("1:a0:");

  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0], "a");
  EXPECT_EQ((*result)[1], "");
}

TEST(LuaExportsPayloadTest, DecodeLineArrayRejectsEmptyArray) {
  auto result = payload::decodeLineArray("");

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("at least one line"), std::string::npos);
}

TEST(LuaExportsPayloadTest, DecodeLineArrayRejectsNewlineBytes) {
  auto result = payload::decodeLineArray("3:a\nb");

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("newline byte"), std::string::npos);
}

TEST(LuaExportsPayloadTest, DecodeLineArrayRejectsNulBytes) {
  const std::string payloadWithNul("3:a\0b", 5);
  auto result = payload::decodeLineArray(payloadWithNul);

  ASSERT_FALSE(result.has_value());
  EXPECT_NE(result.error().message.find("NUL byte"), std::string::npos);
}
