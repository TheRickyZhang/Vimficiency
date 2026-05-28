// Primitive-level oracle property test for insert-mode `<CR>` and `<BS>`,
// against Neovim with autoindent enabled (Neovim's default).
//
// `EditInterpreter::applyEdit` handles `<CR>` and `<BS>` while in Insert mode
// when replaying optimizer-emitted typed completions. The optimizer's
// `ChangeGoalHandler` bakes autoindent semantics into its sequences (e.g.,
// post-`<CR>` cursor lands at the autoindent column, `<BS>` consumes autoindent
// to the previous shiftwidth boundary). If `EditInterpreter` diverges from
// Vim on these primitives, every replayed typed completion is silently wrong.
//
// This test pins the contract: simulate a small Insert-mode entry+chars+`<Esc>`
// program and require buffer/cursor/mode to match Neovim exactly.

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

constexpr string_view INSERT_ALPHABET = "ab ";

auto InsertLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             INSERT_ALPHABET.begin(), INSERT_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto InsertLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(InsertLineDomain(0, 6))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct InsertNewlineSpec {
  vector<string> lines;
  int cursorIndex;
  string typed;  // characters typed in Insert mode (may include <CR>, <BS>)
};

// Concatenate a sequence of tokens into one Insert-mode body.
string joinTokens(const vector<string>& toks) {
  string out;
  for (const auto& t : toks) out += t;
  return out;
}

auto InsertNewlineSpecDomain() {
  return fuzztest::StructOf<InsertNewlineSpec>(
      InsertLinesDomain(1, 3),
      fuzztest::InRange<int>(0, 50),
      fuzztest::Map(joinTokens,
                    fuzztest::VectorOf(fuzztest::ElementOf<string>({
                        "a", "b", " ", "<CR>", "<BS>",
                    })).WithMinSize(1).WithMaxSize(6)));
}

class VerifyInsertNewline {
 public:
  void InsertNewlineMatchesOracle(const InsertNewlineSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    // Enter Insert at cursor, type the body, then exit. `i` keeps the cursor
    // exactly where it is; trailing `<Esc>` exits with the standard col-- if
    // col > 0.
    string seq = "i" + spec.typed + "<Esc>";

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "insert <CR>/<BS> primitive");
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyInsertNewline, InsertNewlineMatchesOracle)
    .WithDomains(InsertNewlineSpecDomain());
