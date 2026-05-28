// Primitive-level oracle property tests for counted edits that the
// TransformExplorer actually emits: counted word edits (Ndw/Nde/Ndb/Ndge with
// big-word variants) and counted char delete (Nx) on single-line buffers
// (the explorer constrains these to range.end.line == cursor.line, so
// multi-line word edits aren't emitted), plus counted line delete (Ndd) on
// multi-line buffers.
//
// `{count}{edit}` does NOT mean `{edit}` repeated `count` times — see
// dev/core/counted-edit-semantics.md. The oracle is the only ground truth.

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

// Word chars only — punctuation introduces extra word boundaries whose
// counted-motion semantics involve subtle Vim quirks (e.g., `Ndge` from inside
// one word with N exceeding the available prev-word ends has direction-and-
// content-specific FAIL behavior we don't fully model). The TransformExplorer
// only emits counted edits where all `count` motions succeed, so we don't need
// to test the FAIL boundary here.
constexpr string_view COUNTED_ALPHABET = "abc ";

auto CountedLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             COUNTED_ALPHABET.begin(), COUNTED_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto CountedLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(CountedLineDomain(1, 8))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

// Single-line counted word/char edits.
struct CountedWordSpec {
  string line;
  int cursorCol;
  int count;
  int commandKind;
};

auto CountedWordSpecDomain() {
  // Long line + cursor anywhere within it. Vim's counted word motions clamp
  // gracefully (mid-iteration `dec_cursor()==-1` returns OK; FAIL only when
  // the FIRST `dec_cursor` of an iteration hits start-of-buffer) so partial
  // counts succeed even at boundaries.
  return fuzztest::StructOf<CountedWordSpec>(
      CountedLineDomain(24, 32),
      fuzztest::InRange<int>(0, 31),
      fuzztest::InRange<int>(2, 5),
      fuzztest::InRange<int>(0, 8));
}

string countedWordCommand(int count, int kind) {
  static constexpr array<string_view, 9> commands = {
      "dw", "dW", "de", "dE",      // forward word
      "db", "dB", "dge", "dgE",    // backward word
      "x",                          // char
  };
  string base(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
  return to_string(count) + base;
}

class VerifyCountedWord {
 public:
  void CountedWordEditsMatchOracle(const CountedWordSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor(0, clamp(spec.cursorCol, 0,
                              max(0, static_cast<int>(spec.line.size()) - 1)));
    string seq = countedWordCommand(spec.count, spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "counted word edit primitive");
  }

 private:
  NeovimOracle oracle_{};
};

// Multi-line counted line delete (Ndd) — multiple lines actually involved.
struct CountedLineSpec {
  vector<string> lines;
  int cursorIndex;
  int count;
};

auto CountedLineSpecDomain() {
  return fuzztest::StructOf<CountedLineSpec>(
      CountedLinesDomain(1, 5),
      fuzztest::InRange<int>(0, 200),
      fuzztest::InRange<int>(2, 9));
}

class VerifyCountedLine {
 public:
  void CountedLineEditsMatchOracle(const CountedLineSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = to_string(spec.count) + "dd";

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "counted line edit primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyCountedWord, CountedWordEditsMatchOracle)
    .WithDomains(CountedWordSpecDomain());

FUZZ_TEST_F(VerifyCountedLine, CountedLineEditsMatchOracle)
    .WithDomains(CountedLineSpecDomain());
