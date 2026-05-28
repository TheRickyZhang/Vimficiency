// Primitive-level oracle property tests for the scroll motions the optimizer
// actually searches: `<C-d>` and `<C-u>` (half-page, driven by 'scroll'), with
// optional counts. Their cursor movement is a fixed 'scroll' offset clamped to
// the buffer — viewport-independent, so it matches Vim exactly.
//
// `<C-f>`/`<C-b>` (full page) are deliberately NOT covered: their cursor
// landing depends on the window's topline, which the minimal-state model does
// not track. They are excluded from optimizer search (MovementToSpec.cpp) and
// only approximated by the interpreter; see the caveat in MovementInterpreter.
//
// 'scroll' is window-local, so the oracle side uses the specialized
// NeovimOracle::simulateScroll entry point (the general simulate() path is
// untouched) and windowHeight() to mirror the fixed headless height into the
// model's NavContext.

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Interpreter/MovementInterpreter.h"
#include "Property/PropertyDomains.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"
#include "Utils/NeovimOracle.h"

using namespace std;

namespace {

struct ScrollSpec {
  vector<string> lines;
  int cursorIndex;
  int scrollAmount;
  int commandKind;  // 0..1 -> <C-d>/<C-u>
  int count;        // 0 = no count prefix
};

auto ScrollSpecDomain() {
  return fuzztest::StructOf<ScrollSpec>(
      PropertyDomains::LineVecDomain(1, 60, 0, 12),
      fuzztest::InRange<int>(0, 800),
      fuzztest::InRange<int>(1, 40),
      fuzztest::InRange<int>(0, 1),  // C-d / C-u only (the searched scroll motions)
      fuzztest::InRange<int>(0, 5));
}

string scrollCommand(int kind, int count) {
  static constexpr array<string_view, 2> commands = {"<C-d>", "<C-u>"};
  string prefix = count > 0 ? to_string(count) : "";
  return prefix + string(commands[clamp(kind, 0, 1)]);
}

class VerifyScrollMotions {
 public:
  void ScrollMotionsMatchOracle(const ScrollSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = lines.cursorFromFlatIndexClamped(spec.cursorIndex);
    int height = oracle_.windowHeight();
    int scroll = clamp(spec.scrollAmount, 1, height);
    NavContext nav(height, scroll);
    string seq = scrollCommand(spec.commandKind, spec.count);

    CursorPos modelCursor = simulateMovements(cursor, seq, lines, nav);
    SimulationResult nvim =
        oracle_.simulateScroll(lines, cursor.line, cursor.col, seq, scroll);

    SCOPED_TRACE(::testing::Message()
                 << "seq='" << seq << "' from " << cursor
                 << " height=" << height << " scroll=" << scroll
                 << "\ninitial=" << lines);

    // Scroll motions never change buffer text or mode.
    EXPECT_EQ(nvim.lines, lines);
    EXPECT_EQ(nvim.mode, Mode::Normal);
    EXPECT_EQ(nvim.row, modelCursor.line);
    EXPECT_EQ(nvim.col, modelCursor.col);
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyScrollMotions, ScrollMotionsMatchOracle)
    .WithDomains(ScrollSpecDomain());
