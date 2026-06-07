#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ApprovalTestUtils.h"
#include "Keyboard/Config.h"
#include "Optimizer/DiffPlanner/CharDiff.h"
#include "Optimizer/DiffPlanner/DiffState.h"
#include "Optimizer/DiffPlanner/TreeDiff.h"
#include "Types/Lines.h"

using namespace std;
using namespace CharDiff;

namespace {

string esc(string_view text) {
  string out;
  for (char c : text) out += (c == '\n') ? string("\\n") : string(1, c);
  return out;
}

// Longest common substring of a and b — the content a REPLACE region deletes and
// retypes unchanged (pure waste). Surfaces the over-merge directly.
string longestCommonSubstr(string_view a, string_view b) {
  int best = 0, bestEnd = 0;
  vector<vector<int>> dp(a.size() + 1, vector<int>(b.size() + 1, 0));
  for (int i = 1; i <= (int)a.size(); i++)
    for (int j = 1; j <= (int)b.size(); j++)
      if (a[i - 1] == b[j - 1]) {
        dp[i][j] = dp[i - 1][j - 1] + 1;
        if (dp[i][j] > best) { best = dp[i][j]; bestEnd = i; }
      }
  return string(a.substr(bestEnd - best, best));
}

// One plan: a `head: cost ...` summary then its region lines. Templated so both
// CharDiff::RegionBreakdown and TreeDiff::RegionBreakdown print identically. A
// REPLACE's `[retyped ...]` is content deleted and typed back unchanged — the
// over-merge to diagnose.
template <typename Region>
void printPlan(ostream& out, const string& head, const vector<Region>& regions, double total) {
  double delSum = 0, insSum = 0, moveSum = 0;
  for (const Region& r : regions) {
    delSum += r.del;
    insSum += r.ins;
    moveSum += r.move;
  }
  out << "  " << head << ": cost " << total << " = diffs " << regions.size()
      << " + del " << delSum << " + ins " << insSum << " + move " << moveSum << "\n";
  for (int i = 0; i < (int)regions.size(); i++) {
    const Region& r = regions[i];
    const char* kind = r.diff.isPureInsertion() ? "insert"
                     : r.diff.isPureDeletion()  ? "delete"
                                                : "replace";
    out << "    [" << i << "] " << kind
        << " \"" << esc(r.diff.deletedText) << "\" -> \"" << esc(r.diff.insertedText) << "\""
        << "  P=1 del=" << r.del << " ins=" << r.ins << " move=" << r.move;
    string common = longestCommonSubstr(r.diff.deletedText, r.diff.insertedText);
    if (!common.empty())
      out << "  [retyped \"" << esc(common) << "\" = " << common.size() << "]";
    out << "\n";
  }
}

// The top-K partitions the K-best DP weighed for a case, ascending by cost. Plan
// 0 is the optimum `calculate` returns; the rest are the alternatives it
// outranked and the margin it did so by.
string renderTopPlans(string_view name, string_view initialText, string_view goalText, int K) {
  const Lines initial = Lines::unflatten(initialText);
  const Lines goal = Lines::unflatten(goalText);
  const Config config = Config::uniform();
  CostOptions options;
  options.maxPlans = K;
  const vector<CostBreakdown> bds = calculateBreakdown(initial, goal, config, options);

  ostringstream out;
  out << name << "  (top " << K << ")\n";
  out << esc(initialText) << "  ->  " << esc(goalText) << "\n";
  for (int p = 0; p < (int)bds.size(); p++)
    printPlan(out, "plan " + to_string(p), bds[p].regions, bds[p].total);
  return out.str();
}

// CharDiff's top-K partitions against TreeDiff's single partition. When CharDiff
// over-merges, its surgical split still appears as a lower-ranked plan, so the
// regression is the *margin* between plan 0 and the split. Per-planner `cost` is
// each planner's own model — comparable across CharDiff plans, not across
// planners.
string renderCompare(string_view name, string_view initialText, string_view goalText, int K) {
  const Lines initial = Lines::unflatten(initialText);
  const Lines goal = Lines::unflatten(goalText);
  const Config config = Config::uniform();
  CostOptions options;
  options.maxPlans = K;
  const vector<CharDiff::CostBreakdown> charBds =
      CharDiff::calculateBreakdown(initial, goal, config, options);
  const TreeDiff::CostBreakdown treeBd =
      TreeDiff::calculateBreakdown(initial, goal, config, {});

  ostringstream out;
  out << name << "\n";
  out << esc(initialText) << "  ->  " << esc(goalText) << "\n";
  for (int p = 0; p < (int)charBds.size(); p++)
    printPlan(out, "CHAR plan " + to_string(p), charBds[p].regions, charBds[p].total);
  printPlan(out, "TREE", treeBd.regions, treeBd.total);
  return out.str();
}

}  // namespace

// CharDiff's keep-vs-retype decisions and their costs on the optimum plan.
// `[retyped ...]` is content deleted and typed back unchanged — the over-merge.
TEST(CharDiffApproval, KeepVsRetypePenalties) {
  ostringstream out;
  out << renderTopPlans("over-merge: two char edits across a space",
                        "abc xyz", "aXc xYz", 1) << "\n";
  out << renderTopPlans("one-char gap between two edits",
                        "abcde", "aXcYe", 1) << "\n";
  out << renderTopPlans("two edits keeping a whole word",
                        "aaa bbb ccc", "xxx bbb yyy", 1) << "\n";
  out << renderTopPlans("rename twice keeping ' + '",
                        "x + x", "y + y", 1) << "\n";
  out << renderTopPlans("clean single word change",
                        "the quick fox", "the slow fox", 1) << "\n";
  out << renderTopPlans("clean trailing deletion",
                        "hello world", "hello", 1) << "\n";
  out << renderTopPlans("clean interior insertion",
                        "abc", "abXc", 1) << "\n";
  verifyText(out.str());
}

// Residual regressions after the delete-oracle fix: multi-edit cases where
// CharDiff over-merges short matched gaps into one retyping REPLACE (plan 0)
// while TreeDiff stays surgical. The K-best view makes the cost the regression
// turns on explicit: CharDiff's own surgical split sits one or more plans below
// plan 0, and the margin is the gap the insert-side miscalibration still loses.
TEST(CharDiffApproval, ResidualOverMerge) {
  ostringstream out;
  out << "-- REGRESSION: short matched gaps over-merge --\n\n";
  out << renderCompare("three edits over short gaps",
                       "ehfaf beeac edgae", "effaf eeac gdgae", 3) << "\n";
  out << renderCompare("two word edits keeping a word",
                       "aaa bbb ccc", "xxx bbb yyy", 3) << "\n";
  out << renderCompare("first+last char, short kept gap",
                       "ab cd ef", "Xb cd eY", 3) << "\n";
  out << "-- CONTRAST: longer matched gap, CHAR splits correctly --\n\n";
  // The kept gap "oo bar ba" is long enough that retyping loses, so CharDiff's
  // plan 0 already matches TreeDiff. The over-merge is gap-length bounded.
  out << renderCompare("first+last char, long kept gap",
                       "foo bar baz", "Xoo bar baY", 3) << "\n";
  verifyText(out.str());
}

// The K-best DP's competing partitions per case, ascending by cost — the
// diagnostic view into why one partition wins. For the keep-a-word case this
// shows the whole-line REPLACE (plan 0) sitting just below the two-surgical-edits
// split, the entire premise of the keep-vs-retype calibration.
TEST(CharDiffApproval, TopKPlans) {
  ostringstream out;
  out << renderTopPlans("two word edits keeping a word",
                        "aaa bbb ccc", "xxx bbb yyy", 4) << "\n";
  out << renderTopPlans("two char edits across a space",
                        "abc xyz", "aXc xYz", 4) << "\n";
  out << renderTopPlans("first+last char, short kept gap",
                        "ab cd ef", "Xb cd eY", 4) << "\n";
  out << renderTopPlans("clean single word change",
                        "the quick fox", "the slow fox", 3) << "\n";
  out << renderTopPlans("trailing deletion",
                        "hello world", "hello", 3) << "\n";
  verifyText(out.str());
}
