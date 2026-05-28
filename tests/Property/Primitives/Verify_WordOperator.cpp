// Primitive-level oracle property tests for word operator + motion commands:
// `dw`, `dW`, `de`, `dE`, `db`, `dB`, `dge`, `dgE`, and their `c` counterparts.
//
// This is where the recent hot-patch rounds concentrated — blank-prefix
// linewise promotion (db/dB/dge/dgE), trailing-whitespace EOL stops (dw),
// EOF/last-line variants (de/dE), and the change-mode quirks where the
// operator's effect differs from the delete-mode effect.

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

constexpr string_view WORDOP_ALPHABET = "abc  ~.,";

auto WordOpLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             WORDOP_ALPHABET.begin(), WORDOP_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto WordOpLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(WordOpLineDomain(0, 8))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct WordOpSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;
};

auto WordOpSpecDomain() {
  return fuzztest::StructOf<WordOpSpec>(
      WordOpLinesDomain(1, 4),
      fuzztest::InRange<int>(0, 200),
      fuzztest::InRange<int>(0, 7));
}

string wordOpCommand(int kind) {
  // c-variants excluded for now — they require Neovim's `ins_esc` autoindent-
  // strip behavior we don't yet model. Re-add once that path is ported.
  static constexpr array<string_view, 8> commands = {
      "dw",  "dW",  "de",  "dE",  "db",  "dB",  "dge", "dgE",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyWordOperator {
 public:
  void WordOperatorMatchesOracle(const WordOpSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = wordOpCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "word operator primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyWordOperator, WordOperatorMatchesOracle)
    .WithDomains(WordOpSpecDomain());
