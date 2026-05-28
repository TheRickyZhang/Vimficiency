// Primitive-level oracle property tests for paragraph text objects:
// `dap`, `dip`, `cap`, `cip` under d/c operators across multi-line buffers
// containing blank lines (paragraph separators).

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

constexpr string_view PARAGRAPH_ALPHABET = "abc ";

auto ParagraphLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             PARAGRAPH_ALPHABET.begin(), PARAGRAPH_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto ParagraphLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(ParagraphLineDomain(0, 4))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct ParagraphTextObjSpec {
  vector<string> lines;
  int cursorIndex;
  int commandKind;
};

auto ParagraphTextObjSpecDomain() {
  return fuzztest::StructOf<ParagraphTextObjSpec>(
      ParagraphLinesDomain(1, 6),
      fuzztest::InRange<int>(0, 200),
      fuzztest::InRange<int>(0, 3));
}

string paragraphTextObjCommand(int kind) {
  static constexpr array<string_view, 4> commands = {
      "dap", "dip", "cap<Esc>", "cip<Esc>",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyParagraphTextObjects {
 public:
  void ParagraphTextObjectsMatchOracle(const ParagraphTextObjSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    string seq = paragraphTextObjCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "paragraph text object primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyParagraphTextObjects, ParagraphTextObjectsMatchOracle)
    .WithDomains(ParagraphTextObjSpecDomain());
