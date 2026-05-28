// Primitive-level oracle property test for `.` (dot repeat). Performs an
// edit, navigates by a controlled amount that won't trigger `:normal!`'s
// abort-on-motion-failure, then `.` — and checks the final state matches
// Neovim. Pins the contract for `Edit::applyEdit`'s `lastEditCmd` storage +
// parse round-trip.
//
// `:normal!` aborts the rest of the sequence at the first failed motion
// (e.g. `j` at the last line, `x` on an empty line), so the inputs are
// shaped to keep every step legal: lines are wide and dense enough that the
// fixed `hh` motion always lands within content.

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

constexpr string_view DOT_ALPHABET = "abc";

auto DotLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             DOT_ALPHABET.begin(), DOT_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

struct DotRepeatSpec {
  string line;
  int cursorCol;
  int commandKind;
};

auto DotRepeatSpecDomain() {
  // Long enough line that any cursor + the fixed `hh` motion stays inside.
  return fuzztest::StructOf<DotRepeatSpec>(
      DotLineDomain(12, 16),
      fuzztest::InRange<int>(4, 11),
      fuzztest::InRange<int>(0, 8));
}

string dotEditCommand(int kind) {
  static constexpr array<string_view, 9> commands = {
      "x",
      "dw", "dW", "de", "dE",
      "db", "dB", "dge", "dgE",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyDotRepeat {
 public:
  void DotRepeatMatchesOracle(const DotRepeatSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor(0, clamp(spec.cursorCol, 0,
                              static_cast<int>(spec.line.size()) - 1));
    // edit, move two cells left (always legal on a dense line), `.`.
    string seq = dotEditCommand(spec.commandKind) + "hh.";

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "dot repeat primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyDotRepeat, DotRepeatMatchesOracle)
    .WithDomains(DotRepeatSpecDomain());
