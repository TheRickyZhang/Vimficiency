#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ApprovalTestUtils.h"
#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/VimDiff.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Types/Lines.h"

using namespace std;
using namespace VimDiff;

namespace {

// One case as a markdown fixture (`.md`), readable rendered and raw. Buffer/diff
// text is literal inside code fences — no keystroke glyphs (`␣`/`⇥`/`↵` mean key
// presses; see SequenceDisplay.cpp). An empty diff side emits no line, so `->`
// sits flush; a newline emits a blank line. That keeps empty distinct from a
// newline with no delimiter (trade-off: a bare trailing/in-line space isn't
// separately flagged). `renderCase` shows the top-K plans the DP weighed; Plan 1
// is the optimum `calculate` returns.
string renderCase(string_view name, string_view initialText, string_view goalText, int K = 3) {
  const Lines initial = Lines::unflatten(initialText);
  const Lines goal = Lines::unflatten(goalText);
  const Config config = Config::uniform();
  CostOptions options;
  options.maxPlans = K;
  const vector<CostBreakdown> bds = calculateBreakdown(initial, goal, config, options);

  ostringstream out;
  out << "# " << name << "\n\n";
  out << "**initial**\n```\n" << initialText << "\n```\n\n";
  out << "**final**\n```\n" << goalText << "\n```\n\n";
  out << "_Edit costs: delete / insert / move_\n";
  for (int p = 0; p < (int)bds.size(); p++) {
    out << "\n### Plan " << (p + 1) << ": cost " << bds[p].total << "\n";
    for (const RegionBreakdown& r : bds[p].regions) {
      out << "\n```\n";
      if (!r.diff.deletedText.empty()) out << r.diff.deletedText << "\n";
      out << "->";
      if (!r.diff.insertedText.empty()) out << "\n" << r.diff.insertedText;
      out << "\n```\n`" << r.del << " / " << r.ins << " / " << r.move << "`\n";
    }
  }
  return out.str();
}

}  // namespace

// One case per approval file (one verifyMarkdown each) so each result diffs in
// isolation. `renderCase` shows the top-K partitions the DP weighed, Plan 1 being
// the optimum `calculate` returns; a REPLACE that re-types an unchanged middle is
// the over-merge to watch for.

TEST(VimDiffApproval, TwoCharEditsAcrossSpace) {
  verifyMarkdown(renderCase("two char edits across a space", "abc xyz", "aXc xYz"));
}

TEST(VimDiffApproval, OneCharGapBetweenEdits) {
  verifyMarkdown(renderCase("one-char gap between two edits", "abcde", "aXcYe"));
}

// The optimum keeps "bbb": retyping the whole line straddles the collapsed kept
// block (no single `dd`), so the surgical two-edit split wins.
TEST(VimDiffApproval, TwoWordEditsKeepingWord) {
  verifyMarkdown(renderCase("two edits keeping a whole word", "aaa bbb ccc", "xxx bbb yyy"));
}

TEST(VimDiffApproval, RenameTwiceKeepingPlus) {
  verifyMarkdown(renderCase("rename twice keeping ' + '", "x + x", "y + y"));
}

TEST(VimDiffApproval, CleanSingleWordChange) {
  verifyMarkdown(renderCase("clean single word change", "the quick fox", "the slow fox"));
}

TEST(VimDiffApproval, TrailingDeletion) {
  verifyMarkdown(renderCase("clean trailing deletion", "hello world", "hello"));
}

TEST(VimDiffApproval, InteriorInsertion) {
  verifyMarkdown(renderCase("clean interior insertion", "abc", "abXc"));
}

TEST(VimDiffApproval, ThreeEditsOverShortGaps) {
  verifyMarkdown(renderCase("three edits over short gaps",
                        "ehfaf beeac edgae", "effaf eeac gdgae"));
}

// Short kept gap "b cd e" that still clears the collapse gate, so the optimum is
// the surgical first+last char split keeping the middle.
TEST(VimDiffApproval, FirstLastCharShortGap) {
  verifyMarkdown(renderCase("first+last char, short kept gap", "ab cd ef", "Xb cd eY"));
}

// Long kept gap "oo bar ba": retyping it loses by far, so the optimum is the
// surgical first+last char split.
TEST(VimDiffApproval, FirstLastCharLongGap) {
  verifyMarkdown(renderCase("first+last char, long kept gap", "foo bar baz", "Xoo bar baY"));
}

// Span-local: "bbbbbbb" is one word delete (2) even though "abbbbbbba" is one
// global word — the clamp-to-start that the tree boundaries can't express.
TEST(VimDiffApproval, SpanLocalDeleteInsideWord) {
  verifyMarkdown(renderCase("span-local delete inside one word", "abbbbbbba", "aa"));
}

// Counted whole-line delete: removing two middle lines is one `2dd`, not 2x dd.
TEST(VimDiffApproval, CountedMultiLineDelete) {
  verifyMarkdown(renderCase("counted multi-line delete",
                        "one\ntwo\nthree\nfour", "one\nfour"));
}
