// tests/Benchmarks/BenchUtils.cpp

#include "BenchUtils.h"

using namespace std;

// Format value with ** markers if it triggered the stop condition
inline string fmtSearched(const SearchStats& s) {
  string val = to_string(s.nodesExplored);
  return (s.stopReason == SearchStopReason::MaxNodesReached) ? "*" + val + "*" : val;
}

inline string fmtFound(const SearchStats& s) {
  string val;
  if (s.isRangeSearch()) {
    // Range search: show "unique/total" (e.g., "6/6" or "3/10")
    val = to_string(s.uniquePositionsFound) + "/" + to_string(s.resultsFound);
  } else {
    // Single-goal search: just show total results
    val = to_string(s.resultsFound);
  }
  bool triggered = (s.stopReason == SearchStopReason::MaxResultsFound ||
                    s.stopReason == SearchStopReason::AllResultsFound);
  return triggered ? "*" + val + "*" : val;
}

inline string fmtRemain(const SearchStats& s) {
  string val = to_string(s.queueSizeAtStop);
  return (s.stopReason == SearchStopReason::FullyExplored) ? "*" + val + "*" : val;
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
