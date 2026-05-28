// Primitive-level oracle property tests for sentence motions and operators:
// `)`, `(`, `d)`, `d(`. Driven through the interpreter and compared to Neovim.
// Uses VimCore::findSentenceForOperator (a faithful port of Neovim's findsent
// with raw past-EOL semantics preserved).
//
// `c)`/`c(` are deliberately excluded for now — they pass through the same
// sentence operator endpoint, but the trailing `<Esc>` triggers Neovim's
// indent-autoremove behavior (strips autoindent that wasn't user-typed), a
// separate Vim semantic our interpreter doesn't yet model. Re-add once the
// `ins_esc` autoindent-strip path is ported.
//
// Alphabet: letters, spaces, sentence terminators `.`, `!`, `?`. Excludes
// closer chars (`)`, `]`, `"`) — they technically work via the new findsent
// port, but make oracle traces noisy and aren't the highest-signal coverage
// for this test.

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

constexpr string_view SENTENCE_ALPHABET = "abc   .!?";

auto SentenceLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             SENTENCE_ALPHABET.begin(), SENTENCE_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto SentenceLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(SentenceLineDomain(0, 10))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct SentenceCaseSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;  // index into command table
};

auto SentenceCaseSpecDomain() {
  return fuzztest::StructOf<SentenceCaseSpec>(
      SentenceLinesDomain(1, 5),
      fuzztest::InRange<int>(0, 200),
      fuzztest::InRange<int>(0, 3));
}

string sentenceCommand(int kind) {
  static constexpr array<string_view, 4> commands = {
      ")", "(", "d)", "d(",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifySentenceCommands {
 public:
  void SentenceCommandsMatchOracle(const SentenceCaseSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = sentenceCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "sentence command primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifySentenceCommands, SentenceCommandsMatchOracle)
    .WithDomains(SentenceCaseSpecDomain());
