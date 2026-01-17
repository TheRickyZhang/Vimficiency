// EditOptimizer: A* search for optimal Vim editing sequences
//
// Uses A* search over (buffer, position, mode) states to find optimal
// ways to transform text. The deletion search finds optimal ways to
// clear buffer from any starting position.

#include "EditOptimizer.h"

#include "Boundary/EditBoundary.h"
#include "Editor/Edit.h"
#include "Editor/NavContext.h"
#include "Keyboard/CharToKeys.h"
#include "Keyboard/MotionToKeys.h"
#include "State/EditState.h"
#include "State/RunningEffort.h"
#include "Utils/Debug.h"
#include "Utils/NoChar.h"
#include "VimCore/VimUtils.h"

#include <queue>
#include <unordered_map>
#include <optional>

using namespace std;

// =============================================================================
// Heuristic for A* search
// =============================================================================

double EditOptimizer::heuristic(const EditState& s, const OptimizerParams& params) const {
  double total = 0;
  for (const auto& line : s.lines) {
    // Total chars + new line
    total += line.size() + 1;
  }
  return total;
}

// =============================================================================
// tryReplacement - replacement strategy for same-length transformations
// =============================================================================

// Helper: compute effort for a sequence string
static double computeSequenceCost(const string& seq, const Config& config) {
  RunningEffort effort;
  for (char c : seq) {
    auto it = CHAR_TO_KEYS.find(c);
    if (it != CHAR_TO_KEYS.end()) {
      effort.append(it->second, config);
    }
  }
  // Handle special sequences
  if (seq.find("<Esc>") != string::npos) {
    effort.append({Key::Key_Esc}, config);
  }
  return effort.getEffort(config);
}



// =============================================================================
// optimizeEdit - main entry point
// =============================================================================

EditResult EditOptimizer::optimizeEdit(const Lines& startLines, const Lines& endLines,
                                        EditBoundary editBoundary,
                                        const optional<OptimizerParams>& paramsOverride) {
  EditResult result;

  // Try J, gJ
  // if(startLines == \s. \n \s., endLines == " ") apply J to any position from previous line

  // Try replace
  if(Lines::sameLineLengths(startLines, endLines)) {
    // if(endLines.size() == 1 && all_same_char) { use {cnt}r } 

    // Could try R but it's pretty much overshadowed by c, s
    // mini search of repeatedly applying r with l, {cnt}l, or f{char}
  }


  // Calculate total positions
  int totalPositions = 0;
  for (const auto& line : startLines) {
    totalPositions += line.empty() ? 1 : line.size();
  }

  // Set up pq, state exploration
  /*
   *
   * if(canDeleteLine) explore dd
   * if(canExploreLeft) ...
   * if(canExploreRight) ...\
   * if(canExplore word motions)
   * if(canExplore text object motions)
   *
   * 
   * // Not worth exploring since main benefit of movement is leveraged better from CompositionOptimizer level
   * if(canExplore quote motions)
   * if(canExplore bracket motions)
  */

  return result;
}
