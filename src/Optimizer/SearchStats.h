#pragma once

#include <ostream>
#include <string>
#include <vector>

enum class SearchStopReason {
  Unknown,
  QueueExhausted,    // Search space fully explored
  NodeLimitReached,  // Hit maxNodesExplored
  AllResultsFound,   // Found results for all positions (EditOptimizer)
  MaxResultsReached, // Found maxResults (for single-goal search)
};

inline std::string to_string(SearchStopReason reason) {
  switch (reason) {
    case SearchStopReason::QueueExhausted: return "QueueExhausted";
    case SearchStopReason::NodeLimitReached: return "NodeLimitReached";
    case SearchStopReason::AllResultsFound: return "AllResultsFound";
    case SearchStopReason::MaxResultsReached: return "MaxResultsReached";
    default: return "Unknown";
  }
}

// Debug: tracks position + sequence for understanding exploration
struct ExploredState {
  int line;
  int col;
  std::string sequence;  // How we got here
};

struct SearchStats {
  SearchStopReason stopReason = SearchStopReason::Unknown;
  int nodesExplored = 0;
  int resultsFound = 0;
  int motionsEmitted = 0;  // Total motions generated across all states
  int statesSkipped = 0;   // States skipped due to staleness

  // Debug: optionally collect explored states (expensive, only for debugging)
  std::vector<ExploredState> exploredStates;

  double avgMotionsPerState() const {
    return nodesExplored > 0 ? static_cast<double>(motionsEmitted) / nodesExplored : 0;
  }

  friend std::ostream& operator<<(std::ostream& os, const SearchStats& s) {
    os << "nodes=" << s.nodesExplored
       << " results=" << s.resultsFound
       << " motions=" << s.motionsEmitted
       << " skipped=" << s.statesSkipped
       << " stop=" << to_string(s.stopReason);
    return os;
  }
};
