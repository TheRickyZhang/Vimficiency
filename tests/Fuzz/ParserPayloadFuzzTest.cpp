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

void DecodeLengthPrefixedStringsRoundTripsAcceptedPayloads(string encoded) {
  auto decoded = payload::decodeLengthPrefixedStrings(encoded);
  if (!decoded) return;

  auto reparsed = payload::decodeLengthPrefixedStrings(encodeFields(*decoded));
  ASSERT_TRUE(reparsed.has_value()) << reparsed.error().message;
  EXPECT_EQ(*reparsed, *decoded);
}

void DecodeLineArrayAcceptsOnlyValidLines(string encoded) {
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

void SequenceParserErrorsStayWithinInput(string sequence) {
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

FUZZ_TEST(PayloadFuzzTest, DecodeLengthPrefixedStringsRoundTripsAcceptedPayloads)
    .WithDomains(fuzztest::String().WithMaxSize(512))
    .WithSeeds({"", "0:", "1:a", "2:ab", "3:a\nb", "999:x"});

FUZZ_TEST(PayloadFuzzTest, DecodeLineArrayAcceptsOnlyValidLines)
    .WithDomains(fuzztest::String().WithMaxSize(512))
    .WithSeeds({"", "0:", "1:a", "2:ab", "3:a\nb", "999:x"});

FUZZ_TEST(SequenceParserFuzzTest, SequenceParserErrorsStayWithinInput)
    .WithDomains(fuzztest::String().WithMaxSize(256))
    .WithSeeds({"", "w", "3dw", "ciwhello<Esc>", "<C-d>", "\xffw"});
