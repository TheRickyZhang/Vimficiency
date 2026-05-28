// Primitive-level oracle property tests for word text objects.
//
// Uses VimCore::currentWord (a faithful port of Vim's current_word from
// textobject.c) plus operator-level extensions applied at the
// wordTextObjectRange layer:
//   - daw extension through trailing empty line when starting on blank
//   - op_delete linewise promotion when inclusive end lands on NUL of empty
//   - clearopbeep cursor placement on failed text-object motions (in
//     interpreter), with no insert-mode transition for c
//   - <Esc> as Normal-mode no-op for the post-failure-c case
//
// All daw/diw/daW/diW and caw/ciw/caW/ciW variants are covered.

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

constexpr string_view WORD_ALPHABET = "abc  .,;_";

auto WordLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             WORD_ALPHABET.begin(), WORD_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto WordLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(WordLineDomain(0, 8))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct TextObjectSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;
};

auto TextObjectSpecDomain() {
  return fuzztest::StructOf<TextObjectSpec>(
      WordLinesDomain(1, 4),
      fuzztest::InRange<int>(0, 200),
      fuzztest::InRange<int>(0, 7));
}

string textObjectCommand(int kind) {
  static constexpr array<string_view, 8> commands = {
      "daw", "diw", "daW", "diW",
      "caw<Esc>", "ciw<Esc>", "caW<Esc>", "ciW<Esc>",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyWordTextObjects {
 public:
  void WordTextObjectsMatchOracle(const TextObjectSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = textObjectCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "word text object primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyWordTextObjects, WordTextObjectsMatchOracle)
    .WithDomains(TextObjectSpecDomain());
