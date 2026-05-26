// Safety property: payload and sequence decoders reject arbitrary bytes cleanly
// or return stable, bounded results.

#include <string>
#include <string_view>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/SequenceParser.h"
#include "LuaExports/Common.h"

using namespace std;

namespace payload = VF::LuaExports::payload;

namespace {

template <typename Fields>
string encodeFields(const Fields& fields) {
  string encoded;
  for (const auto& field : fields) {
    encoded += payload::encodeField(string_view(field.data(), field.size()));
  }
  return encoded;
}

void LengthPrefixedPayloadsRoundTrip(string encoded) {
  auto decoded = payload::decodeLengthPrefixedStrings(encoded);
  if (!decoded) return;

  auto reparsed = payload::decodeLengthPrefixedStrings(encodeFields(*decoded));
  ASSERT_TRUE(reparsed) << reparsed.error().message;
  EXPECT_EQ(*reparsed, *decoded);
}

void LineArraysRoundTrip(string encoded) {
  auto decoded = payload::decodeLineArray(encoded);
  if (!decoded) return;

  ASSERT_FALSE(decoded->empty());
  for (const string& line : *decoded) {
    EXPECT_FALSE(line.contains('\n'));
    EXPECT_FALSE(line.contains('\0'));
  }

  auto reparsed = payload::decodeLineArray(encodeFields(*decoded));
  ASSERT_TRUE(reparsed) << reparsed.error().message;
  EXPECT_EQ(*reparsed, *decoded);
}

void SequenceTokensAreStable(string sequence) {
  auto parsed = parseSequence(sequence);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, sequence.size());
    EXPECT_FALSE(formatSequenceParseError(parsed.error()).empty());
    return;
  }

  auto parsedStrings = parseSequenceStrings(sequence);
  ASSERT_TRUE(parsedStrings) << formatSequenceParseError(parsedStrings.error());
  ASSERT_EQ(parsedStrings->size(), parsed->size());
  for (size_t i = 0; i < parsed->size(); i++) {
    const auto& token = (*parsed)[i];
    EXPECT_EQ((*parsedStrings)[i], string(token.token));
  }
}

}  // namespace

FUZZ_TEST(PayloadSafetyTest, LengthPrefixedPayloadsRoundTrip)
    .WithDomains(fuzztest::String().WithMaxSize(512))
    .WithSeeds({
        "",

        // Valid frames.
        "0:",
        "1:a",
        "2:ab",
        "3:a\nb",
        "5:hello",
        "1:a0:",
        "0:1:a",
        "3:a:b",
        "1::",
        string("3:a\0b", 5),

        // Malformed frames.
        "1:",
        "1:ab",
        "2:a",
        "2:abc",
        "0:x",
        "999:x",
        ":abc",
        "x:abc",
        "-1:a",
        "+1:a",
        "1",
        "1;",
        " 1:a",

        // Overflow.
        "18446744073709551615:x",
        "18446744073709551616:x",
        "999999999999999999999999999999:x",
    });

FUZZ_TEST(PayloadSafetyTest, LineArraysRoundTrip)
    .WithDomains(fuzztest::String().WithMaxSize(512))
    .WithSeeds({
        "",

        // Valid arrays.
        "0:",
        "1:a",
        "3:abc",
        "1:a0:",
        "0:1:a",
        "0:0:",

        // Framing succeeds; line validation rejects contents.
        "3:a\nb",
        "1:\n",
        string("3:a\0b", 5),
        string("1:\0", 3),

        // Malformed frames.
        "1:",
        "1:ab",
        "2:a",
        "0:x",
        ":abc",
        "999999999999999999999999999999:x",
    });

FUZZ_TEST(
    SequenceParserSafetyTest,
    SequenceTokensAreStable)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({
        "",
        "w",
        "3dw",
        "ciwhello<Esc>",
        "<C-d>",
        "\xffw",

        // Partial and malformed tokens.
        "<",
        "<C-",
        "<Bad>",
        "f,;",
        "d",
        "r",
        "ra",
        "iabc<Esc>",
        "vwd",
        "999999999999dw",
    });
