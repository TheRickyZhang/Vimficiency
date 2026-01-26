#pragma once

#include <ostream>
#include <string>

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

struct SearchStats {
  SearchStopReason stopReason = SearchStopReason::Unknown;
  int nodesExplored = 0;
  int resultsFound = 0;

  friend std::ostream& operator<<(std::ostream& os, const SearchStats& s) {
    os << "nodes=" << s.nodesExplored
       << " results=" << s.resultsFound
       << " stop=" << to_string(s.stopReason);
    return os;
  }
};
