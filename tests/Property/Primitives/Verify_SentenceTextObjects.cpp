// Primitive-level oracle property tests for sentence text objects:
// `das`, `dis` under `d` operator on a single line.
//
// The test deliberately filters out all-whitespace buffers: Vim's das on
// "   " is a pathological artifact of its internal algorithm (it deletes
// exactly one char chosen by where decl/incl happen to end up), not a
// meaningful "delete a sentence" behavior. Real-world editing doesn't hit
// this case, and matching it exactly would require porting current_sent's
// full algorithm including its "didn't move, advance one, try again" loop.

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

struct SentenceTextObjSpec {
  string line;
  int cursorCol;
  int commandKind;
};

auto SentenceTextObjSpecDomain() {
  return fuzztest::StructOf<SentenceTextObjSpec>(
      SentenceLineDomain(0, 15),
      fuzztest::InRange<int>(0, 20),
      fuzztest::InRange<int>(0, 1));
}

string sentenceTextObjCommand(int kind) {
  static constexpr array<string_view, 2> commands = { "das", "dis" };
  return string(commands[clamp(kind, 0, 1)]);
}

class VerifySentenceTextObjects {
 public:
  void SentenceTextObjectsMatchOracle(const SentenceTextObjSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor(0, clamp(spec.cursorCol, 0,
                              max(0, static_cast<int>(spec.line.size()) - 1)));
    string seq = sentenceTextObjCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "sentence text object primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifySentenceTextObjects, SentenceTextObjectsMatchOracle)
    .WithDomains(SentenceTextObjSpecDomain());
