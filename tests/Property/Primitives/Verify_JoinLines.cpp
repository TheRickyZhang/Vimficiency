// Primitive-level oracle property test for VimCore::joinLines and joinLineRange.
//
// Compares the primitive's effect on (lines, cursor) against Neovim's J/gJ/{n}J/{n}gJ.
// Any divergence surfaces here at the primitive layer rather than as an
// optimizer integration mismatch with a long emitted sequence.
//
// Known gaps these tests will surface until Neovim's `do_join` is ported in
// full (Phase 5 of the systematic plan):
//   - counted-J cursor placement when joining through whitespace-only lines
//     and trailing-whitespace separators.
//   - `gJ` cursor lands on last char of original first line, not on the
//     concatenation point (off-by-one for non-empty first lines).
//   - Comment-leader stripping under default `formatoptions+=j` (alphabet
//     excludes `#`, `-`, `*`, `>` for now — re-add once leader rule is ported).

#include <algorithm>
#include <string>
#include <vector>

#include <fuzztest/fuzztest.h>
#include <gtest/gtest.h>

#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Utils/NeovimOracle.h"
#include "VimCore/VimEditUtils.h"

using namespace std;

namespace {

// Alphabet covers the characters that drive Vim's join behavior in the cases
// our primitive currently models: letters (ordinary content), space (next-line
// indent stripping), closing punctuation (no-space-before rule), sentence
// terminators (joinspaces double-space rule), and tabs (no-space-after-tab
// rule).
//
// Intentionally excluded: `#`, `-`, `*`, `>` — characters Neovim's default
// `formatoptions+=j` treats as comment leaders. Vim strips matching leaders
// on join; we only implement a narrow `#-after-dash` case. Re-add them once
// full leader-pattern detection is ported from Neovim `get_leader_len`.
constexpr string_view JOIN_ALPHABET = "abcde  )],;.!?\t";

auto JoinLineDomain(int minLen, int maxLen) {
  return fuzztest::StringOf(fuzztest::ElementOf(vector<char>(
             JOIN_ALPHABET.begin(), JOIN_ALPHABET.end())))
      .WithMinSize(minLen)
      .WithMaxSize(maxLen);
}

auto JoinLinesDomain(int minLines, int maxLines) {
  return fuzztest::VectorOf(JoinLineDomain(0, 10))
      .WithMinSize(minLines)
      .WithMaxSize(maxLines);
}

struct SingleJoinSpec {
  vector<string> lines;
  int cursorIndex;
  bool addSpace;
};

struct MultiJoinSpec {
  vector<string> lines;
  int cursorIndex;
  bool addSpace;
  int extraJoinCount;
};

auto SingleJoinSpecDomain() {
  // 2+ lines so a join is well-defined.
  return fuzztest::StructOf<SingleJoinSpec>(
      JoinLinesDomain(2, 5),
      fuzztest::InRange<int>(0, 200),
      fuzztest::Arbitrary<bool>());
}

auto MultiJoinSpecDomain() {
  // 3+ lines (counted joins of 3+ lines); extraJoinCount maps to count in [3,5].
  return fuzztest::StructOf<MultiJoinSpec>(
      JoinLinesDomain(3, 6),
      fuzztest::InRange<int>(0, 200),
      fuzztest::Arbitrary<bool>(),
      fuzztest::InRange<int>(0, 2));
}

CursorPos clampCursorAboveLastLine(const Lines& lines, int flatIndex) {
  CursorPos pos = lines.cursorFromFlatIndexClamped(flatIndex);
  // joinLines requires at least one line below the cursor.
  if (pos.line >= lines.lastLine()) {
    pos = CursorPos(lines.lastLine() - 1,
                    min(pos.col, max(0, static_cast<int>(lines[lines.lastLine() - 1].size()) - 1)));
  }
  return pos;
}

CursorPos clampCursorForMultiJoin(const Lines& lines, int flatIndex, int joinLineCount) {
  // Need joinLineCount lines starting at cursor.line, i.e. cursor.line + joinLineCount - 1 <= lastLine.
  int maxLine = lines.lastLine() - (joinLineCount - 1);
  if (maxLine < 0) maxLine = 0;
  CursorPos pos = lines.cursorFromFlatIndexClamped(flatIndex);
  if (pos.line > maxLine) {
    pos = CursorPos(maxLine,
                    min(pos.col, max(0, static_cast<int>(lines[maxLine].size()) - 1)));
  }
  return pos;
}

string joinSequence(int joinLineCount, bool addSpace) {
  // Vim's J/gJ joins 2 lines; {n}J/{n}gJ joins n lines = n-1 join operations.
  string seq;
  if (joinLineCount > 2) seq = to_string(joinLineCount);
  seq += addSpace ? "J" : "gJ";
  return seq;
}

class VerifyJoinLines {
 public:
  void SingleJoinMatchesOracle(const SingleJoinSpec& spec) {
    Lines lines(spec.lines);
    CursorPos cursor = clampCursorAboveLastLine(lines, spec.cursorIndex);

    Lines primitiveLines = lines;
    CursorPos primitivePos = cursor;
    VimCore::joinLines(primitiveLines, primitivePos, spec.addSpace);

    string seq = joinSequence(/*joinLineCount=*/2, spec.addSpace);
    SimulationResult nvim =
        oracle_.simulate(lines, cursor.line, cursor.col, seq);

    SCOPED_TRACE(::testing::Message()
                 << "join seq='" << seq << "' from " << cursor
                 << " initial=" << lines);
    EXPECT_EQ(primitiveLines, nvim.lines);
    EXPECT_EQ(primitivePos.line, nvim.row);
    EXPECT_EQ(primitivePos.col, nvim.col);
  }

  void MultiJoinMatchesOracle(const MultiJoinSpec& spec) {
    int joinLineCount = 3 + spec.extraJoinCount;  // 3..5
    Lines lines(spec.lines);
    if (lines.lastLine() < joinLineCount - 1) {
      joinLineCount = lines.lastLine() + 1;
      if (joinLineCount < 2) return;
    }
    CursorPos cursor = clampCursorForMultiJoin(lines, spec.cursorIndex, joinLineCount);

    Lines primitiveLines = lines;
    CursorPos primitivePos = cursor;
    if (joinLineCount == 2) {
      VimCore::joinLines(primitiveLines, primitivePos, spec.addSpace);
    } else {
      VimCore::joinLineRange(primitiveLines, primitivePos, joinLineCount, spec.addSpace);
    }

    string seq = joinSequence(joinLineCount, spec.addSpace);
    SimulationResult nvim =
        oracle_.simulate(lines, cursor.line, cursor.col, seq);

    SCOPED_TRACE(::testing::Message()
                 << "join seq='" << seq << "' from " << cursor
                 << " initial=" << lines);
    EXPECT_EQ(primitiveLines, nvim.lines);
    EXPECT_EQ(primitivePos.line, nvim.row);
    EXPECT_EQ(primitivePos.col, nvim.col);
  }

 private:
  NeovimOracle oracle_{};
};

}  // namespace

FUZZ_TEST_F(VerifyJoinLines, SingleJoinMatchesOracle)
    .WithDomains(SingleJoinSpecDomain());

FUZZ_TEST_F(VerifyJoinLines, MultiJoinMatchesOracle)
    .WithDomains(MultiJoinSpecDomain());
