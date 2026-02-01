// tests/Benchmarks/BenchUtils.cpp

#include "BenchUtils.h"

using namespace std;

// Helper: append N stars to value (e.g., "42***" for count=3)
inline string appendStars(const string& val, int count) {
  if (count == 0) return val;
  return val + string(count, '*');
}

// Format value with star markers based on star counts (for multi-seed aggregation)
// Star counts > 0 indicate how many runs triggered that condition
// For single runs (count=0), fall back to checking stopReason
inline string fmtSearched(const SearchStats& s) {
  string val = to_string(s.nodesExplored);
  if (s.searchedStarCount > 0) {
    return appendStars(val, s.searchedStarCount);
  }
  // Single run fallback
  return (s.stopReason == SearchStopReason::MaxNodesReached) ? val + "*" : val;
}

inline string fmtFound(const SearchStats& s) {
  if (s.isRangeSearch()) {
    // Range search: show "unique/total" with separate star markers
    // uniqueStarCount -> stars on unique part (AllResultsFound)
    // foundStarCount -> stars on total part (MaxResultsFound)
    string uniquePart = to_string(s.uniquePositionsFound);
    string totalPart = to_string(s.resultsFound);

    if (s.uniqueStarCount > 0 || s.foundStarCount > 0) {
      // Multi-seed: append stars to appropriate part
      uniquePart = appendStars(uniquePart, s.uniqueStarCount);
      totalPart = appendStars(totalPart, s.foundStarCount);
    } else {
      // Single run fallback
      if (s.stopReason == SearchStopReason::AllResultsFound) {
        uniquePart += "*";
      } else if (s.stopReason == SearchStopReason::MaxResultsFound) {
        totalPart += "*";
      }
    }
    return uniquePart + "/" + totalPart;
  } else {
    // Single-goal search: just show total results with stars
    string val = to_string(s.resultsFound);
    if (s.foundStarCount > 0) {
      return appendStars(val, s.foundStarCount);
    }
    // Single run fallback
    bool triggered = (s.stopReason == SearchStopReason::MaxResultsFound ||
                      s.stopReason == SearchStopReason::AllResultsFound);
    return triggered ? val + "*" : val;
  }
}

inline string fmtRemain(const SearchStats& s) {
  string val = to_string(s.queueSizeAtStop);
  if (s.remainStarCount > 0) {
    return appendStars(val, s.remainStarCount);
  }
  // Single run fallback
  return (s.stopReason == SearchStopReason::FullyExplored) ? val + "*" : val;
}

void printTableHeaderWithStats(const string& paramName) {
  cout << setw(12) << left << paramName
       << "|" << setw(11) << right << "Avg (ms)"
       << " |" << setw(11) << "Min (ms)"
       << " |" << setw(11) << "Max (ms)"
       << " |" << setw(11) << "Median"
       << " |" << setw(10) << "Searched"
       << " |" << setw(10) << "Found"
       << " |" << setw(8) << "Remain"
       << endl;
  cout << string(12, '-') << "|" << string(12, '-')
       << "|" << string(12, '-') << "|" << string(12, '-')
       << "|" << string(12, '-') << "|" << string(11, '-')
       << "|" << string(11, '-') << "|" << string(9, '-') << endl;
}

void printRowWithStats(const string& label, const BenchmarkResult& result) {
  cout << setw(12) << left << label
       << "|" << setw(11) << right << fixed << setprecision(2) << result.timing.avgMs
       << " |" << setw(11) << result.timing.minMs
       << " |" << setw(11) << result.timing.maxMs
       << " |" << setw(11) << result.timing.medianMs
       << " |" << setw(10) << fmtSearched(result.search)
       << " |" << setw(10) << fmtFound(result.search)
       << " |" << setw(8) << fmtRemain(result.search)
       << endl;
}

void printComparisonHeader(const string& paramName) {
  cout << setw(12) << left << paramName
       << "| " << setw(8) << right << "A (ms)"
       << " | " << setw(8) << "Searched"
       << " | " << setw(8) << "Found"
       << " | " << setw(8) << "Remain"
       << " | " << setw(8) << "B (ms)"
       << " | " << setw(8) << "Searched"
       << " | " << setw(8) << "Found"
       << " | " << setw(8) << "Remain"
       << " | " << setw(8) << "Speedup"
       << endl;
  cout << string(12, '-')
       << "|" << string(10, '-') << "|" << string(10, '-') << "|" << string(10, '-')
       << "|" << string(10, '-')
       << "|" << string(10, '-') << "|" << string(10, '-') << "|" << string(10, '-')
       << "|" << string(10, '-')
       << "|" << string(10, '-') << endl;
}

void printComparisonRow(const string& label, const ComparisonResult& cmp) {
  cout << setw(12) << left << label
       << "| " << setw(8) << right << fixed << setprecision(2) << cmp.a.timing.avgMs
       << " | " << setw(8) << fmtSearched(cmp.a.search)
       << " | " << setw(8) << fmtFound(cmp.a.search)
       << " | " << setw(8) << fmtRemain(cmp.a.search)
       << " | " << setw(8) << cmp.b.timing.avgMs
       << " | " << setw(8) << fmtSearched(cmp.b.search)
       << " | " << setw(8) << fmtFound(cmp.b.search)
       << " | " << setw(8) << fmtRemain(cmp.b.search)
       << " | " << setw(7) << setprecision(1) << cmp.speedupPercent << "%"
       << endl;
}
