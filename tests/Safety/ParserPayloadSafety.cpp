// Safety property: payload and sequence decoders must handle arbitrary bytes by
// rejecting cleanly or returning canonical, bounded results. Accepted payloads
// must round-trip through the encoder; accepted sequence tokens must have stable
// string representations.

#include <string>
#include <string_view>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/SequenceParser.h"
#include "LuaExports/Common.h"

using namespace std;

namespace payload = VF::LuaExports::payload;

namespace {

// `.WithSeeds(...)` gives deterministic smoke inputs. Longer FuzzTest campaigns
// still generate from the domain and use those seeds as corpus starting points.
// Payload seeds include valid frames because random strings rarely preserve the
// `<len>:<bytes>` relationship long enough to reach accepted-input logic.
// Separate FUZZ_TESTs are intentional here: the framing decoder, line-array
// wrapper, and sequence parser have different accepted-output contracts.

template <typename Fields>
string encodeFields(const Fields& fields) {
  string encoded;
  for (const auto& field : fields) {
    encoded += payload::encodeField(string_view(field.data(), field.size()));
  }
  return encoded;
}

void LengthPrefixedPayloadsRoundTripAfterSuccessfulDecode(string encoded) {
  auto decoded = payload::decodeLengthPrefixedStrings(encoded);
  if (!decoded) return;

  auto reparsed = payload::decodeLengthPrefixedStrings(encodeFields(*decoded));
  ASSERT_TRUE(reparsed.has_value()) << reparsed.error().message;
  EXPECT_EQ(*reparsed, *decoded);
}

void LineArrayDecoderRejectsInvalidLinesOrRoundTrips(string encoded) {
  auto decoded = payload::decodeLineArray(encoded);
  if (!decoded) return;

  ASSERT_FALSE(decoded->empty());
  for (const string& line : *decoded) {
    EXPECT_EQ(line.find('\n'), string::npos);
    EXPECT_EQ(line.find('\0'), string::npos);
  }

  auto reparsed = payload::decodeLineArray(encodeFields(decoded.value()));
  ASSERT_TRUE(reparsed.has_value()) << reparsed.error().message;
  EXPECT_EQ(*reparsed, *decoded);
}

void SequenceParserRejectsInvalidInputOrReturnsStableTokens(string sequence) {
  auto parsed = parseSequence(sequence);
  if (!parsed) {
    EXPECT_LE(parsed.error().offset, sequence.size());
    EXPECT_FALSE(formatSequenceParseError(parsed.error()).empty());
    return;
  }

  auto parsedStrings = parseSequenceStrings(sequence);
  ASSERT_TRUE(parsedStrings.has_value())
      << formatSequenceParseError(parsedStrings.error());
  ASSERT_EQ(parsedStrings->size(), parsed->size());
  for (size_t i = 0; i < parsed->size(); i++) {
    EXPECT_EQ((*parsedStrings)[i], string((*parsed)[i].token));
  }
}

}  // namespace

FUZZ_TEST(PayloadSafetyTest, LengthPrefixedPayloadsRoundTripAfterSuccessfulDecode)
    .WithDomains(fuzztest::String().WithMaxSize(512))
    .WithSeeds({
        "",

        // Valid single and multi-field payloads.
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

        // Length mismatch and malformed header shapes.
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

        // Numeric overflow and long decimal headers.
        "18446744073709551615:x",
        "18446744073709551616:x",
        "999999999999999999999999999999:x",
    });

FUZZ_TEST(PayloadSafetyTest, LineArrayDecoderRejectsInvalidLinesOrRoundTrips)
    .WithDomains(fuzztest::String().WithMaxSize(512))
    .WithSeeds({
        "",

        // Valid line arrays, including leading/trailing empty lines.
        "0:",
        "1:a",
        "3:abc",
        "1:a0:",
        "0:1:a",
        "0:0:",

        // Framing succeeds, but line-array validation should reject contents.
        "3:a\nb",
        "1:\n",
        string("3:a\0b", 5),
        string("1:\0", 3),

        // Reuse malformed frame shapes against the line-array wrapper.
        "1:",
        "1:ab",
        "2:a",
        "0:x",
        ":abc",
        "999999999999999999999999999999:x",
    });

FUZZ_TEST(
    SequenceParserSafetyTest,
    SequenceParserRejectsInvalidInputOrReturnsStableTokens)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({
        "",
        "w",
        "3dw",
        "ciwhello<Esc>",
        "<C-d>",
        "\xffw",

        // Token boundaries and partial angle/control notations.
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
