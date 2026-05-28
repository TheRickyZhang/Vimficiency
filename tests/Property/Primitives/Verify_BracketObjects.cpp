// Primitive-level oracle property tests for bracket text objects:
// `a(`/`i(`/`)`, `a{`/`i{`/`}`, `a[`/`i[`/`]`, `a<`/`i<`/`>` (and `b`/`B`
// aliases) under `d` operator.
//
// Bracket text objects find the matching brackets around the cursor across
// multiple lines. Distinct logic from quote/word objects.

#include <algorithm>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/InterpreterModelReplay.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

using namespace std;

namespace {

constexpr string_view BRACKET_ALPHABET = "abc ()[]{}<>";

auto BracketLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             BRACKET_ALPHABET.begin(), BRACKET_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto BracketLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(BracketLineDomain(0, 8))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct BracketSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;
};

auto BracketSpecDomain() {
  return fuzztest::StructOf<BracketSpec>(
      BracketLinesDomain(1, 3),
      fuzztest::InRange<int>(0, 100),
      fuzztest::InRange<int>(0, 15));
}

string bracketCommand(int kind) {
  // Delete-only. Open and close char both work as the same text object;
  // we exercise both forms.
  static constexpr array<string_view, 16> commands = {
      "da(", "di(", "da)", "di)",
      "da{", "di{", "da}", "di}",
      "da[", "di[", "da]", "di]",
      "da<", "di<", "da>", "di>",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyBracketObjects {
 public:
  void BracketObjectsMatchOracle(const BracketSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = bracketCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "bracket text object primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyBracketObjects, BracketObjectsMatchOracle)
    .WithDomains(BracketSpecDomain());
