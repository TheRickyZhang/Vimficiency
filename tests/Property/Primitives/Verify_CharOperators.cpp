// Primitive-level oracle property tests for character-level operators:
// `x`, `X`, `~`, `r{ch}` (replace char). Includes counted variants.
//
// These are simple operations but have subtle Vim cursor-placement and
// EOL/buffer-edge behavior we want to lock down.

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

constexpr string_view CHAR_OP_ALPHABET = "abcd  .,";

auto LineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             CHAR_OP_ALPHABET.begin(), CHAR_OP_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

struct CharOpSpec {
  string line;
  int cursorCol;
  int commandKind;
  int count;       // 0 = no count, 1..5 = explicit count
  char replacement;
};

auto CharOpSpecDomain() {
  return fuzztest::StructOf<CharOpSpec>(
      LineDomain(1, 10),
      fuzztest::InRange<int>(0, 12),
      fuzztest::InRange<int>(0, 3),
      fuzztest::InRange<int>(0, 5),
      fuzztest::ElementOf(vector<char>{'a', 'z', '.', ' '}));
}

string charOpCommand(int kind, int count, char replacement) {
  string countStr = (count < 2) ? "" : to_string(count);
  switch (clamp(kind, 0, 3)) {
    case 0: return countStr + "x";
    case 1: return countStr + "X";
    case 2: return countStr + "~";
    default: return countStr + "r" + replacement;
  }
}

class VerifyCharOperators {
 public:
  void CharOperatorsMatchOracle(const CharOpSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor(0, clamp(spec.cursorCol, 0,
                              max(0, static_cast<int>(spec.line.size()) - 1)));
    string seq = charOpCommand(spec.commandKind, spec.count, spec.replacement);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "char operator primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyCharOperators, CharOperatorsMatchOracle)
    .WithDomains(CharOpSpecDomain());
