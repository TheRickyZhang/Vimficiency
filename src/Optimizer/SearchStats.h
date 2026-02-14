#pragma once

#include <ostream>
#include <string>
#include <vector>

enum class SearchStopReason {
  Unknown,
  FullyExplored,    // Search space fully explored
  MaxNodesReached,  // Hit maxNodesExplored
  AllResultsFound,   // Found results for all positions (EditOptimizer)
  MaxResultsFound, // Found maxResults (for single-goal search)
};

inline std::string to_string(SearchStopReason reason) {
  switch (reason) {
    case SearchStopReason::FullyExplored: return "QueueExhausted";
    case SearchStopReason::MaxNodesReached: return "NodeLimitReached";
    case SearchStopReason::AllResultsFound: return "AllResultsFound";
    case SearchStopReason::MaxResultsFound: return "MaxResultsReached";
    default: return "Unknown";
  }
}

// Short version for compact table display
inline std::string toShortString(SearchStopReason reason) {
  switch (reason) {
    case SearchStopReason::FullyExplored: return "Queue";
    case SearchStopReason::MaxNodesReached: return "Limit";
    case SearchStopReason::AllResultsFound: return "All";
    case SearchStopReason::MaxResultsFound: return "Max";
    default: return "?";
  }
}

// Debug: tracks position + sequence for understanding exploration
struct ExploredState {
  int line;
  int col;
  double effort;
  std::string sequence;  // How we got here
};

struct SearchStats {
  // Core stats - these determine stop reason
  SearchStopReason stopReason = SearchStopReason::Unknown;
  int nodesExplored = 0;        // Compared against maxNodesExplored
  int resultsFound = 0;         // Total results (paths found)
  int uniquePositionsFound = -1; // Unique positions (-1 = N/A for single-goal searches)
  int queueSizeAtStop = 0;      // 0 means FullyExplored

  // Star counts for multi-seed aggregation (how many runs triggered each condition)
  // 0 = not aggregated (single run), >0 = count of runs that triggered
  int searchedStarCount = 0;    // MaxNodesReached
  int foundStarCount = 0;       // MaxResultsFound (total results limit)
  int uniqueStarCount = 0;      // AllResultsFound (all unique positions in range)
  int remainStarCount = 0;      // FullyExplored (queue exhausted)

  // Suffix cache stats (only populated by suffix-cached search)
  int cacheHits = 0;
  int cacheEntries = 0;
  int cachePopulations = 0;

  // Sub-optimizer aggregate stats (CompositionOptimizer only)
  int motionNodesExplored = 0;  // Sum of nodesExplored across all MotionOptimizer calls
  int editNodesExplored = 0;    // Sum of nodesExplored across all EditOptimizer calls

  // Debug only (has runtime cost, only enable when needed)
  int motionsEmitted = 0;
  int statesSkipped = 0;
  std::vector<ExploredState> exploredStates;

  // Check if this is a range search (has meaningful uniquePositionsFound)
  bool isRangeSearch() const { return uniquePositionsFound >= 0; }

  double avgMotionsPerState() const {
    return nodesExplored > 0 ? static_cast<double>(motionsEmitted) / nodesExplored : 0;
  }

  friend std::ostream& operator<<(std::ostream& os, const SearchStats& s) {
    os << "nodes=" << s.nodesExplored
       << " results=" << s.resultsFound;
    if (s.isRangeSearch()) {
      os << " unique=" << s.uniquePositionsFound;
    }
    os << " queue=" << s.queueSizeAtStop
       << " stop=" << to_string(s.stopReason);
    if (s.motionNodesExplored > 0 || s.editNodesExplored > 0) {
      os << " motionNodes=" << s.motionNodesExplored
         << " editNodes=" << s.editNodesExplored;
    }
    return os;
  }
};
