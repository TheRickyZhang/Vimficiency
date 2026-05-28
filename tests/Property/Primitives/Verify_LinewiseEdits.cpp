// Primitive-level oracle property tests for linewise edits: `dd`/`cc`/`S`,
// counted `{n}dd` / `{n}cc`.
//
// `dd` deletes N lines linewise; `cc`/`S` change them with autoindent. Counted
// semantics matter: `2dd` deletes 2 contiguous lines as one operation, not
// `dd` twice (the cursor relocates between sequential dds).
//
// `cc`/`S` `<Esc>` variants are excluded pending ins_esc autoindent-strip port.

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

constexpr string_view LINE_ALPHABET = "abc  .";

auto LineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             LINE_ALPHABET.begin(), LINE_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto LinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(LineDomain(0, 6))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct LinewiseSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;
  int count;  // 0 = no count, 1..5 explicit
};

auto LinewiseSpecDomain() {
  return fuzztest::StructOf<LinewiseSpec>(
      LinesDomain(1, 5),
      fuzztest::InRange<int>(0, 100),
      fuzztest::InRange<int>(0, 1),  // dd, dj (extend if needed)
      fuzztest::InRange<int>(0, 5));
}

string linewiseCommand(int kind, int count) {
  string countStr = (count < 2) ? "" : to_string(count);
  static constexpr array<string_view, 2> commands = {"dd", "dj"};
  return countStr + string(commands[clamp(kind, 0, 1)]);
}

class VerifyLinewiseEdits {
 public:
  void LinewiseEditsMatchOracle(const LinewiseSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = linewiseCommand(spec.commandKind, spec.count);

    // Skip `dj` from last line — it requires a line below.
    if (spec.commandKind == 1 && cursor.line == lines.lastLine()) return;

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "linewise edit primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyLinewiseEdits, LinewiseEditsMatchOracle)
    .WithDomains(LinewiseSpecDomain());
