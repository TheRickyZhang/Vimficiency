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

// Strips the leading newline and trailing whitespace a `R"(\n...\n  )"` block
// carries, so the readable multi-line literal does not inject an empty first
// line or a trailing blank/space line into the buffer. Interior blank lines and
// indentation are preserved.
string trimBlock(string_view s) {
  size_t begin = 0, end = s.size();
  if (begin < end && s[begin] == '\n') ++begin;
  while (end > begin && (s[end - 1] == '\n' || s[end - 1] == ' ' || s[end - 1] == '\t')) --end;
  return string(s.substr(begin, end - begin));
}

// One case as a markdown fixture (`.md`), readable rendered and raw. Buffer/diff
// text is literal inside code fences — no keystroke glyphs (`␣`/`⇥`/`↵` mean key
// presses; see SequenceDisplay.cpp). An empty diff side emits no line, so `->`
// sits flush; a newline emits a blank line. That keeps empty distinct from a
// newline with no delimiter (trade-off: a bare trailing/in-line space isn't
// separately flagged). `renderCase` shows the top-K plans the DP weighed; Plan 1
// is the optimum `calculate` returns.
string renderCase(string_view name, string_view initialText, string_view goalText, int K = 3) {
  const string initialBlock = trimBlock(initialText);
  const string goalBlock = trimBlock(goalText);
  const Lines initial = Lines::unflatten(initialBlock);
  const Lines goal = Lines::unflatten(goalBlock);
  const Config config = Config::uniform();
  CostOptions options;
  options.maxPlans = K;
  const vector<CostBreakdown> bds = calculateBreakdown(initial, goal, config, options);

  ostringstream out;
  out << "# " << name << "\n\n";
  out << "**initial**\n```\n" << initialBlock << "\n```\n\n";
  out << "**final**\n```\n" << goalBlock << "\n```\n\n";
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

// One word changes on one line of a three-line buffer; the kept lines stay out
// of the diff rather than being folded into a whole-buffer retype.
TEST(VimDiffApproval, WordChanges) {
  string initial = R"(
the quick brown fox
jumps over
the lazy dog
  )";
  string goal = R"(
the quick red fox
jumps above
the lazy cat
  )";
  verifyMarkdown(renderCase("clean single word change", initial, goal));
}

// Edits on the first and last lines with an untouched line between them: the
// optimum is two surgical edits, not one REPLACE that re-types the kept line.
TEST(VimDiffApproval, TwoEditsKeepingMiddleLine) {
  string initial = R"(
first old line
keep this untouched
second old line
  )";
  string goal = R"(
abc line here
keep this untouched
def line here
  )";
  verifyMarkdown(renderCase("two edits keeping the middle line", initial, goal));
}

// Same symbol renamed in several places across lines — repeated small
// replacements separated by kept code.
TEST(VimDiffApproval, RenameSymbol) {
  string initial = R"(
int cnt = 0;
cnt = cnt + 1;
return cnt;
  )";
  string goal = R"(
int tot = 0;
tot = tot + 1;
return tot;
  )";
  verifyMarkdown(renderCase("rename symbol across lines", initial, goal));
}

// Per-line value edits keeping each line's prefix key; three regions separated
// by kept text rather than a single block retype.
TEST(VimDiffApproval, Main) {
  string initial = R"(
int main() {
  int n = 0;
  for(int i = 0; i < n; i++) {
    cout << i << endl;
  }
}
  )";
  string goal = R"(
int main() {
  int m = 1;
  for(int i = 0; i < m; i++) {
    cout << i+1 << endl;
  }
}
  )";
  verifyMarkdown(renderCase("per-line value edits", initial, goal));
}

// A new line inserted between two kept lines: a pure insertion region.
TEST(VimDiffApproval, RearrangeLines) {
  string initial = R"(
#include <string>
#include <string_view>
#include <vector>
  )";
  string goal = R"(
#include <vector>
#include <string>
#include <string_view>
  )";
  verifyMarkdown(renderCase("rearrange lines", initial, goal));
}

// Change confined to the interior of a block, keeping the first and last lines
// (the brace frame) untouched.
TEST(VimDiffApproval, ChangeBlockBodyKeepingFrame) {
  string initial = R"(
function f() {
  i++;
}
  )";
  string goal = R"(
function f() {
  return 17;
}
  )";
  verifyMarkdown(renderCase("change block body keeping frame", initial, goal));
}

// Counted whole-line delete: removing two middle lines is one `2dd`, not 2x dd.
TEST(VimDiffApproval, JoinedLines) {
  string initial = R"(
one
two
three
four
five
  )";
  string goal = R"(
one two three
four five
  )";
  verifyMarkdown(renderCase("join lines", initial, goal));
}

// Span-local: "bbbbbbb" is one word delete (2) even though "abbbbbbba" is one
// global word — the clamp-to-start that the tree boundaries can't express.
// Genuinely single-line behavior, so no raw string needed.
TEST(VimDiffApproval, SpanLocalDeleteInsideWord) {
  verifyMarkdown(renderCase("span-local delete inside one word", "abbbbbbba", "aa"));
}
