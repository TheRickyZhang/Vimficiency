#include <gtest/gtest.h>

#include "ApprovalTestUtils.h"
#include "Session/ResultView.h"

namespace {

// Note these currently don't really test anything besides for how the C++ formatter works;
TEST(ResultDisplayApproval, EditedFunctionSavedReport) {
  verifyText(joinLines(VF::SessionView::formatSavedLines(
      "rename_int_n_to_m",
      VF::SessionView::Result{
          .lines = {
              "int main() {",
              "  int n = 1;",
              "  return n;",
              "}",
          },
          .startRow = 1,
          .startCol = 6,
          .endRow = 2,
          .endCol = 10,
          .userSeq = "jciwn<Esc>",
          .userCost = 12.345,
          .optimalResults = {
              {.seq = "jciwm<Esc>", .cost = 4.25},
              {.seq = "2jfndw", .cost = 7.5},
          },
      })));
}

TEST(ResultDisplayApproval, MovementOnlyNoRecommendations) {
  verifyText(joinLines(VF::SessionView::formatSavedLines(
      "movement_only",
      VF::SessionView::Result{
          .lines = {
              "alpha beta",
              "gamma delta",
          },
          .startRow = 0,
          .startCol = 0,
          .endRow = 0,
          .endCol = 6,
          .userCost = 0.0,
      })));
}

}  // namespace
