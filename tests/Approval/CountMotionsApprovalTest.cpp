// Snapshot of the NavOptimizer's count-motion recommendation menus. Replaces
// the hand-asserted "contains 4w" optimizer-integration tests in
// tests/Unit/Commands/CountMotionsTest.cpp: an approval snapshot shows the whole
// ranked menu for each scenario, so any change to what the optimizer surfaces is
// reviewable in the diff. A thin set of hard "this capability exists" assertions
// stays in the unit file as a regression backstop against silent drops.
//
// Output is sequences + landing positions only (no costs) so the snapshot is
// deterministic and free of floating-point formatting. Results are stable-sorted
// by (cost, sequence) so ordering does not depend on the optimizer's internal
// container iteration order.

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ApprovalTestUtils.h"
#include "Boundary/NavBoundary.h"
#include "Keyboard/Config.h"
#include "Optimizer/NavOptimizer/NavOptimizer.h"
#include "Types/CursorPos.h"
#include "Types/Lines.h"
#include "Types/NavContext.h"

using namespace std;

namespace {

string formatMenu(const Lines& lines, CursorPos start, CursorPos end,
                  const string& userSeq) {
  NavOptimizer opt(Config::uniform());
  NavBoundary boundary;
  NavContext navContext;
  vector<LandingResult> results =
      opt.optimize(lines, start, end,
                   NavOptimizerParams{}
                       .withMaxResults(30)
                       .withMaxNodesPopped(20000)
                       .withMaxResultsPerEndPos(2),
                   userSeq, boundary, navContext)
          .getResults();

  stable_sort(results.begin(), results.end(),
              [](const LandingResult& a, const LandingResult& b) {
                if (a.getCost() != b.getCost()) return a.getCost() < b.getCost();
                return a.getSequence().view() < b.getSequence().view();
              });

  // Snapshot only the top recommendations — a focused menu that stays readable
  // and doesn't churn on every deep-result reshuffle.
  constexpr int TOP_N = 8;
  ostringstream out;
  out << "user \"" << userSeq << "\": (" << start.line << "," << start.col
      << ") -> (" << end.line << "," << end.col << ")\n";
  int shown = 0;
  for (const LandingResult& r : results) {
    if (shown++ >= TOP_N) break;
    const CursorPos& p = r.getGoalPos();
    out << "  " << r.getSequence().view() << " -> (" << p.line << "," << p.col
        << ")\n";
  }
  return out.str();
}

const Lines wordLine = {"one two three four five six seven eight"};
const Lines multiWordLines = {
    "first second third fourth",
    "alpha beta gamma delta",
    "",
    "after blank paragraph",
};

}  // namespace

TEST(CountMotionsApproval, ForwardWordCount) {
  verifyText(formatMenu(wordLine, {0, 0}, {0, 19}, "wwww"));
}

TEST(CountMotionsApproval, BackwardWordCount) {
  verifyText(formatMenu(wordLine, {0, 34}, {0, 14}, "bbbb"));
}

TEST(CountMotionsApproval, ForwardWordEndCount) {
  verifyText(formatMenu(wordLine, {0, 0}, {0, 17}, "eeee"));
}

TEST(CountMotionsApproval, BackwardWordEndCount) {
  verifyText(formatMenu(wordLine, {0, 38}, {0, 17}, "gegegege"));
}

TEST(CountMotionsApproval, CrossLineWordCount) {
  verifyText(formatMenu(multiWordLines, {0, 0}, {1, 6}, "jwww"));
}

TEST(CountMotionsApproval, ParagraphCount) {
  verifyText(formatMenu(multiWordLines, {0, 0}, {3, 0}, "}}"));
}

TEST(CountMotionsApproval, SingleWordNoCount) {
  verifyText(formatMenu(wordLine, {0, 0}, {0, 4}, "w"));
}
