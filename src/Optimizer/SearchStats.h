#pragma once

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "BuildConfig.h"
#include "Types/Sequence.h"
#include "Utils/Debug.h"

// VIMF_TRACK_STATES is emitted by CMake as 0 or 1 via BuildConfig.h. Using
// `#if` (not `#ifdef`) means a misspelled flag resolves to 0 and triggers
// -Wundef, rather than silently skipping the "enabled" branch.
constexpr bool SEARCH_TRACE_STATS_ENABLED = VIMF_TRACK_STATES;

enum class SearchStopReason {
  Unknown,
  FullyExplored,
  MaxPopsReached,
  AllResultsFound,
  MaxResultsFound,
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

inline std::string toShortString(SearchStopReason reason) {
  switch (reason) {
    case SearchStopReason::FullyExplored: return "Queue";
    case SearchStopReason::MaxPopsReached: return "Limit";
    case SearchStopReason::AllResultsFound: return "All";
    case SearchStopReason::MaxResultsFound: return "Max";
    default: return "?";
  }
}

struct ExploredState {
  int line;
  int col;
  double effort;
  std::string sequence;
};

class BaseSearchStats {
public:
  SearchStopReason stopReason() const { return stopReason_; }
  int nodesExplored() const { return nodesExplored_; }
  int totalPops() const { return totalPops_; }
  int resultsFound() const { return resultsFound_; }
  int queueSizeAtStop() const { return queueSizeAtStop_; }
  int motionsEmitted() const { return motionsEmitted_; }
  int statesSkipped() const { return statesSkipped_; }
  const std::vector<ExploredState>& exploredStates() const { return exploredStates_; }

  void incrementNodesExplored() { nodesExplored_++; }
  void incrementMotionsEmitted() { motionsEmitted_++; }
  void incrementStatesSkipped() { statesSkipped_++; }

  void maybeRecordExploredState(int line,
                                int col,
                                double effort,
                                const Sequence& sequence) {
    if constexpr (SEARCH_TRACE_STATS_ENABLED) {
      exploredStates_.push_back({line, col, effort, sequence.str()});
    }
  }

  void maybeRecordExploredState(int line,
                                int col,
                                double effort,
                                std::string_view sequence) {
    if constexpr (SEARCH_TRACE_STATS_ENABLED) {
      exploredStates_.push_back({line, col, effort, std::string(sequence)});
    }
  }

  void finalize(int nodesExploredValue,
                       int totalPopsValue,
                       int resultsFoundValue,
                       int queueSizeAtStopValue,
                       SearchStopReason stopReasonValue) {
    nodesExplored_ = nodesExploredValue;
    totalPops_ = totalPopsValue;
    resultsFound_ = resultsFoundValue;
    queueSizeAtStop_ = queueSizeAtStopValue;
    stopReason_ = stopReasonValue;
  }

  void setDebugSummary(int motionsEmittedValue, int statesSkippedValue) {
    motionsEmitted_ = motionsEmittedValue;
    statesSkipped_ = statesSkippedValue;
  }

  void setExploredStates(std::vector<ExploredState> exploredStatesValue) {
    exploredStates_ = std::move(exploredStatesValue);
  }

  void accumulateFrom(const BaseSearchStats& other) {
    nodesExplored_ += other.nodesExplored_;
    totalPops_ += other.totalPops_;
    resultsFound_ += other.resultsFound_;
    queueSizeAtStop_ += other.queueSizeAtStop_;
    motionsEmitted_ += other.motionsEmitted_;
    statesSkipped_ += other.statesSkipped_;
  }

  double avgMotionsPerState() const {
    return nodesExplored_ > 0 ? static_cast<double>(motionsEmitted_) / nodesExplored_ : 0;
  }

  friend std::ostream& operator<<(std::ostream& os, const BaseSearchStats& s) {
    os << "nodes=" << s.nodesExplored_
       << " pops=" << s.totalPops_
       << " results=" << s.resultsFound_
       << " queue=" << s.queueSizeAtStop_
       << " stop=" << to_string(s.stopReason_);
    return os;
  }

protected:
  void setTotalPops(int totalPopsValue) { totalPops_ = totalPopsValue; }
  void setResultsFound(int resultsFoundValue) { resultsFound_ = resultsFoundValue; }
  void setQueueSizeAtStop(int queueSizeAtStopValue) { queueSizeAtStop_ = queueSizeAtStopValue; }
  void setStopReason(SearchStopReason stopReasonValue) { stopReason_ = stopReasonValue; }

private:
  SearchStopReason stopReason_ = SearchStopReason::Unknown;
  int nodesExplored_ = 0;
  int totalPops_ = 0;
  int resultsFound_ = 0;
  int queueSizeAtStop_ = 0;
  int motionsEmitted_ = 0;
  int statesSkipped_ = 0;
  std::vector<ExploredState> exploredStates_;
};

class NavSearchStats : public BaseSearchStats {
public:
  int uniquePositionsFound() const { return uniquePositionsFound_; }
  bool isRangeSearch() const { return uniquePositionsFound_ >= 0; }

  void finalizeSingleGoal(int resultsFoundValue,
                          int queueSizeAtStopValue,
                          int maxResults,
                          int maxNodesPopped,
                          int totalPopsValue,
                          bool queueEmpty) {
    setTotalPops(totalPopsValue);
    setResultsFound(resultsFoundValue);
    setQueueSizeAtStop(queueSizeAtStopValue);
    if (resultsFoundValue >= maxResults) {
      setStopReason(SearchStopReason::MaxResultsFound);
    } else if (totalPopsValue >= maxNodesPopped) {
      setStopReason(SearchStopReason::MaxPopsReached);
    } else if (queueEmpty) {
      setStopReason(SearchStopReason::FullyExplored);
    }
  }

  void finalizeRangeGoal(int resultsFoundValue,
                         int uniquePositionsFoundValue,
                         int queueSizeAtStopValue,
                         int allResultsCount,
                         int maxResults,
                         int maxNodesPopped,
                         int totalPopsValue,
                         bool queueEmpty) {
    setTotalPops(totalPopsValue);
    setResultsFound(resultsFoundValue);
    uniquePositionsFound_ = uniquePositionsFoundValue;
    setQueueSizeAtStop(queueSizeAtStopValue);
    if (uniquePositionsFoundValue >= allResultsCount) {
      setStopReason(SearchStopReason::AllResultsFound);
    } else if (resultsFoundValue >= maxResults) {
      setStopReason(SearchStopReason::MaxResultsFound);
    } else if (totalPopsValue >= maxNodesPopped) {
      setStopReason(SearchStopReason::MaxPopsReached);
    } else if (queueEmpty) {
      setStopReason(SearchStopReason::FullyExplored);
    }
  }

  void accumulateFrom(const NavSearchStats& other) {
    BaseSearchStats::accumulateFrom(other);
    if (other.isRangeSearch()) {
      if (!isRangeSearch()) uniquePositionsFound_ = 0;
      uniquePositionsFound_ += other.uniquePositionsFound_;
    }
  }

  friend std::ostream& operator<<(std::ostream& os, const NavSearchStats& s) {
    os << static_cast<const BaseSearchStats&>(s);
    if (s.isRangeSearch()) {
      os << " unique=" << s.uniquePositionsFound_;
    }
    return os;
  }

private:
  int uniquePositionsFound_ = -1;
};

class TransformSearchStats : public BaseSearchStats {
public:
  int uniquePositionsFound() const { return uniquePositionsFound_; }
  int cacheHits() const { return cacheHits_; }
  int cacheEntries() const { return cacheEntries_; }
  int cachePopulations() const { return cachePopulations_; }
  bool isRangeSearch() const { return uniquePositionsFound_ >= 0; }

  void setRangeSearchUniquePositions(int uniquePositionsFoundValue) {
    uniquePositionsFound_ = uniquePositionsFoundValue;
  }

  void finalize(int nodesExploredValue,
                           int totalPopsValue,
                           int resultsFoundValue,
                           int uniquePositionsFoundValue,
                           int queueSizeAtStopValue,
                           int terminalStartsValue,
                           int totalPositionsValue,
                           int maxResultsValue,
                           int maxNodesPoppedValue,
                           bool queueEmpty) {
    BaseSearchStats::finalize(nodesExploredValue,
                    totalPopsValue,
                    resultsFoundValue,
                    queueSizeAtStopValue,
                    SearchStopReason::Unknown);
    uniquePositionsFound_ = uniquePositionsFoundValue;
    if (terminalStartsValue >= totalPositionsValue) {
      setStopReason(SearchStopReason::AllResultsFound);
    } else if (resultsFoundValue >= maxResultsValue) {
      setStopReason(SearchStopReason::MaxResultsFound);
    } else if (totalPopsValue >= maxNodesPoppedValue) {
      setStopReason(SearchStopReason::MaxPopsReached);
    } else if (queueEmpty) {
      setStopReason(SearchStopReason::FullyExplored);
    }
  }

  void setCacheStats(int cacheHitsValue, int cacheEntriesValue, int cachePopulationsValue) {
    cacheHits_ = cacheHitsValue;
    cacheEntries_ = cacheEntriesValue;
    cachePopulations_ = cachePopulationsValue;
  }

  void accumulateFrom(const TransformSearchStats& other) {
    BaseSearchStats::accumulateFrom(other);
    if (other.isRangeSearch()) {
      if (!isRangeSearch()) uniquePositionsFound_ = 0;
      uniquePositionsFound_ += other.uniquePositionsFound_;
    }
    cacheHits_ += other.cacheHits_;
    cacheEntries_ += other.cacheEntries_;
    cachePopulations_ += other.cachePopulations_;
  }

  friend std::ostream& operator<<(std::ostream& os, const TransformSearchStats& s) {
    os << static_cast<const BaseSearchStats&>(s);
    if (s.isRangeSearch()) {
      os << " unique=" << s.uniquePositionsFound_;
    }
    if (s.cacheHits_ > 0 || s.cacheEntries_ > 0) {
      os << " cache=" << s.cacheHits_ << "/" << s.cacheEntries_;
    }
    return os;
  }

private:
  int uniquePositionsFound_ = -1;
  int cacheHits_ = 0;
  int cacheEntries_ = 0;
  int cachePopulations_ = 0;
};

class CompositionSearchStats : public BaseSearchStats {
public:
  int navNodesExplored() const { return navNodesExplored_; }
  int editNodesExplored() const { return editNodesExplored_; }

  void setNodeBreakdown(int navNodesExploredValue, int editNodesExploredValue) {
    navNodesExplored_ = navNodesExploredValue;
    editNodesExplored_ = editNodesExploredValue;
  }

  void accumulateFrom(const CompositionSearchStats& other) {
    BaseSearchStats::accumulateFrom(other);
    navNodesExplored_ += other.navNodesExplored_;
    editNodesExplored_ += other.editNodesExplored_;
  }

  friend std::ostream& operator<<(std::ostream& os, const CompositionSearchStats& s) {
    os << static_cast<const BaseSearchStats&>(s);
    if (s.navNodesExplored_ > 0 || s.editNodesExplored_ > 0) {
      os << " navNodes=" << s.navNodesExplored_
         << " editNodes=" << s.editNodesExplored_;
    }
    return os;
  }

private:
  int navNodesExplored_ = 0;
  int editNodesExplored_ = 0;
};
