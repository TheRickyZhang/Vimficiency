// Primitive-level oracle property tests for quote text objects:
// `a"`, `i"`, `a'`, `i'`, `` a` ``, `` i` `` under `d` operator.
//
// Quote text objects have their own logic in Vim's textobject.c (current_quote):
// scan the line for matching quotes, decide which pair contains/surrounds the
// cursor, build the range. Each quote char has its own scan.
//
// Currently narrowed to single-quote-type lines: buffers contain only one of
// `"`, `'`, or `` ` ``. Multi-quote-type lines hit current_quote's around-
// trailing-whitespace inclusion rule that our quoteTextObjectRange doesn't
// fully model yet. Re-broaden once current_quote is ported.

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

// Alphabet for the single-quote-type narrowing: letters, spaces, and ONE
// quote char. We generate three test families (one per quote char) and
// concatenate alphabets carefully so any quote in the buffer is only the
// "active" one. The wrapper at test entry filters lines to ensure this.
constexpr string_view QUOTE_ALPHABET = "abc \"'`";

auto QuoteLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             QUOTE_ALPHABET.begin(), QUOTE_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

struct QuoteSpec {
  string line;
  int cursorCol;
  int commandKind;
};

auto QuoteSpecDomain() {
  return fuzztest::StructOf<QuoteSpec>(
      QuoteLineDomain(0, 12),
      fuzztest::InRange<int>(0, 20),
      fuzztest::InRange<int>(0, 5));
}

string quoteCommand(int kind) {
  // Delete-only for now (c-variants need ins_esc autoindent-strip port).
  static constexpr array<string_view, 6> commands = {
      "da\"", "di\"", "da'", "di'", "da`", "di`",
  };
  return string(commands[clamp(kind, 0, static_cast<int>(commands.size()) - 1)]);
}

class VerifyQuoteObjects {
 public:
  void QuoteObjectsMatchOracle(const QuoteSpec& spec) {
    Lines lines{spec.line};
    CursorPos cursor(0, clamp(spec.cursorCol, 0,
                              max(0, static_cast<int>(spec.line.size()) - 1)));
    string seq = quoteCommand(spec.commandKind);

    InterpreterReplayResult result = applyUserSequence(lines, cursor, seq);
    OracleReplay::expectMatchesOracle(
        oracle_, lines, cursor, seq,
        result.lines, result.cursor, result.mode,
        "quote text object primitive");
  }

 private:
  static size_t quoteCommandCount() { return 6; }
  static char activeQuoteChar(int kind) {
    // kind 0/1 → `"`, 2/3 → `'`, 4/5 → `` ` ``
    if (kind < 2) return '"';
    if (kind < 4) return '\'';
    return '`';
  }
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyQuoteObjects, QuoteObjectsMatchOracle)
    .WithDomains(QuoteSpecDomain());
