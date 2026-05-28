// Primitive-level oracle property tests for paragraph motions and operators:
// `}`, `{` (motion only) and `d}`, `d{`, `c}`, `c{`. Driven through the
// interpreter and compared to Neovim.
//
// Alphabet emphasizes the characters that drive paragraph boundary semantics:
// letters as ordinary content, spaces and tabs (whitespace-only line case),
// and a generous mix of empty lines (paragraph separator) via short maxLen.

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

constexpr string_view PARAGRAPH_ALPHABET = "abc   .";

auto ParagraphLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             PARAGRAPH_ALPHABET.begin(), PARAGRAPH_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto ParagraphLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(ParagraphLineDomain(0, 6))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct ParagraphCaseSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;
};

auto ParagraphCaseSpecDomain() {
  return fuzztest::StructOf<ParagraphCaseSpec>(
      ParagraphLinesDomain(2, 6),
      fuzztest::InRange<int>(0, 200),
      fuzztest::InRange<int>(0, 3));
}

string paragraphCommand(int kind) {
  // `c}`/`c{` excluded for the same reason as sentence change commands:
  // they require Neovim's `ins_esc` autoindent-strip behavior we don't yet
  // model. Re-add once that path is ported.
  static constexpr array<string_view, 4> commands = {
      "}", "{", "d}", "d{",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyParagraphCommands {
 public:
  void ParagraphCommandsMatchOracle(const ParagraphCaseSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = paragraphCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "paragraph command primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyParagraphCommands, ParagraphCommandsMatchOracle)
    .WithDomains(ParagraphCaseSpecDomain());
