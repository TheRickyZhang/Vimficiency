// Primitive-level oracle property test for the cleared-shell entry + collapse
// pair in ChangeGoalHandler. The cleared shell is the buffer shape
// `[pre, "", ..., "", suf]` that `isGoalReached` accepts before the typed
// completion runs. From any cursor position in that shell, applying
// `buildClearedShellEntry + buildCollapseSequence + <Esc>` must produce
// `[pre + suf]` with cursor positioned at end-of-pre (col == pre.size() - 1
// after the implicit <Esc> backup, or 0 if pre is empty).

#include <algorithm>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Optimizer/TransformOptimizer/ChangeGoalHandler.h"
#include "Keyboard/KeyedSequence.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "Utils/OracleReplay.h"

using namespace std;

namespace {

// Pre/suf alphabet: letters + space so we exercise leading-whitespace cases
// (which were what made `I` wrong vs `0i`).
constexpr string_view PRE_SUF_ALPHABET = "abc ";

auto LineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             PRE_SUF_ALPHABET.begin(), PRE_SUF_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

struct ClearedShellSpec {
  string pre;
  string suf;
  int middleCount;        // number of empty middle lines [0..3]
  int cursorLineRaw;      // raw, clamped into [0, totalLines)
  int cursorColRaw;       // raw, clamped into line's bounds
};

auto ClearedShellSpecDomain() {
  return fuzztest::StructOf<ClearedShellSpec>(
      LineDomain(0, 6),
      LineDomain(0, 6),
      fuzztest::InRange<int>(0, 3),
      fuzztest::InRange<int>(0, 10),
      fuzztest::InRange<int>(0, 10));
}

class VerifyClearedShellEntry {
 public:
  void EntryAndCollapseMatchOracle(const ClearedShellSpec& spec) {
    int middles = clamp(spec.middleCount, 0, 3);
    int totalLines = 1 + middles + 1;
    // pre line + middles + suf line; if pre and suf are both empty, the
    // shell still has at least 2 lines, which is what the optimizer reaches
    // through the deletion path.

    Lines shell;
    shell.push_back(spec.pre);
    for (int i = 0; i < middles; i++) shell.push_back("");
    shell.push_back(spec.suf);

    int cursorLine = clamp(spec.cursorLineRaw, 0, totalLines - 1);
    int lineLen = static_cast<int>(shell[cursorLine].size());
    int cursorCol = lineLen == 0 ? 0 : clamp(spec.cursorColRaw, 0, lineLen - 1);

    KeyedSequence seq =
        ChangeGoalHandler::buildClearedShellEntry(totalLines, cursorLine, cursorCol);
    seq += ChangeGoalHandler::buildCollapseSequence(totalLines, cursorLine);
    seq += KeyedSequence::Esc;

    string joined = spec.pre + spec.suf;
    Lines expectedLines{joined};
    // After <Esc> in Normal mode, cursor lands on the last char before
    // where it was in insert mode. End-of-pre in insert mode = col pre.size().
    // <Esc> backs up one position, so col = max(0, pre.size() - 1) when pre
    // is non-empty. When pre is empty (col 0 in insert), <Esc> stays at col 0.
    int expectedCol = spec.pre.empty() ? 0 : static_cast<int>(spec.pre.size()) - 1;
    CursorPos expectedPos(0, expectedCol);

    string context = "cleared-shell entry + collapse, pre='" + spec.pre +
                     "' suf='" + spec.suf + "' middles=" + to_string(middles) +
                     " cursor=(" + to_string(cursorLine) + "," + to_string(cursorCol) + ")";

    OracleReplay::expectMatchesOracle(
        oracle_, shell, CursorPos(cursorLine, cursorCol),
        seq.seq.view(), expectedLines, expectedPos, Mode::Normal, context);
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyClearedShellEntry, EntryAndCollapseMatchOracle)
    .WithDomains(ClearedShellSpecDomain());
