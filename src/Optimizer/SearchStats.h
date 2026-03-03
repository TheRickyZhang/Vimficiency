#pragma once

#include <ostream>
#include <string>
#include <vector>

enum class SearchStopReason {
  Unknown,
  FullyExplored,    // Search space fully explored
  MaxPopsReached,  // Hit maxNodesPopped
  AllResultsFound,   // Found results for all positions (EditOptimizer)
  MaxResultsFound, // Found maxResults (for single-goal search)
};

inline std::string to_string(SearchStopReason reason) {
  switch (reason) {
    case SearchStopReason::FullyExplored: return "QueueExhausted";
    case SearchStopReason::MaxPopsReached: return "PopLimitReached";
    case SearchStopReason::AllResultsFound: return "AllResultsFound";
    case SearchStopReason::MaxResultsFound: return "MaxResultsReached";
    default: return "Unknown";
  }
}

// Short version for compact table display
inline std::string toShortString(SearchStopReason reason) {
  switch (reason) {
    case SearchStopReason::FullyExplored: return "Queue";
    case SearchStopReason::MaxPopsReached: return "Limit";
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

struct BaseSearchStats {
  SearchStopReason stopReason = SearchStopReason::Unknown;
  int nodesExplored = 0;
  int totalPops = 0;
  int resultsFound = 0;
  int queueSizeAtStop = 0;

  // Debug only (has runtime cost, only enable when needed)
  int motionsEmitted = 0;
  int statesSkipped = 0;
  std::vector<ExploredState> exploredStates;

  double avgMotionsPerState() const {
    return nodesExplored > 0 ? static_cast<double>(motionsEmitted) / nodesExplored : 0;
  }

  friend std::ostream& operator<<(std::ostream& os, const BaseSearchStats& s) {
    os << "nodes=" << s.nodesExplored
       << " pops=" << s.totalPops
       << " results=" << s.resultsFound
       << " queue=" << s.queueSizeAtStop
       << " stop=" << to_string(s.stopReason);
    return os;
  }
};

struct MotionSearchStats : BaseSearchStats {
  int uniquePositionsFound = -1;
  bool isRangeSearch() const { return uniquePositionsFound >= 0; }

  friend std::ostream& operator<<(std::ostream& os, const MotionSearchStats& s) {
    os << static_cast<const BaseSearchStats&>(s);
    if (s.isRangeSearch()) {
      os << " unique=" << s.uniquePositionsFound;
    }
    return os;
  }
};

struct EditSearchStats : BaseSearchStats {
  int uniquePositionsFound = -1;
  int cacheHits = 0;
  int cacheEntries = 0;

  int cachePopulations = 0;
  bool isRangeSearch() const { return uniquePositionsFound >= 0; }

  friend std::ostream& operator<<(std::ostream& os, const EditSearchStats& s) {
    os << static_cast<const BaseSearchStats&>(s);
    if (s.isRangeSearch()) {
      os << " unique=" << s.uniquePositionsFound;
    }
    if (s.cacheHits > 0 || s.cacheEntries > 0) {
      os << " cache=" << s.cacheHits << "/" << s.cacheEntries;
    }
    return os;
  }
};

struct CompositionSearchStats : BaseSearchStats {
  int motionNodesExplored = 0;
  int editNodesExplored = 0;

  friend std::ostream& operator<<(std::ostream& os, const CompositionSearchStats& s) {
    os << static_cast<const BaseSearchStats&>(s);
    if (s.motionNodesExplored > 0 || s.editNodesExplored > 0) {
      os << " motionNodes=" << s.motionNodesExplored
         << " editNodes=" << s.editNodesExplored;
    }
    return os;
  }
};
