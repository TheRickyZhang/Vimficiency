#pragma once

#include <string>
#include <ostream>
#include <vector>

#include "Editor/Position.h"
#include "State/Sequence.h"

struct Result {
  std::vector<Sequence> sequences;
  double keyCost;

  Result() : keyCost(0) {}
  Result(std::vector<Sequence> seqs, double c) : sequences(std::move(seqs)), keyCost(c) {}

  // Constructor from string (creates single Normal mode sequence)
  Result(const std::string& s, double c) : keyCost(c) {
    if (!s.empty()) {
      sequences.emplace_back(s, Mode::Normal);
    }
  }

  bool isValid() const {
    return !sequences.empty();
  }

  // Get flattened string representation
  std::string getSequenceString() const {
    return flattenSequences(sequences);
  }

  // Get formatted string with mode annotations
  std::string getFormattedSequence() const {
    return formatSequences(sequences);
  }
};

inline std::ostream& operator<<(std::ostream& os, const Result& r) {
  os << r.getSequenceString() << ", " << r.keyCost << "\n";
  return os;
}

// Result with end position, used by optimizeToRange
struct RangeResult {
  std::vector<Sequence> sequences;
  double keyCost;
  Position endPos;

  RangeResult() : keyCost(0), endPos(0, 0) {}
  RangeResult(std::vector<Sequence> seqs, double c, Position p)
    : sequences(std::move(seqs)), keyCost(c), endPos(p) {}

  // Constructor from string
  RangeResult(const std::string& s, double c, Position p) : keyCost(c), endPos(p) {
    if (!s.empty()) {
      sequences.emplace_back(s, Mode::Normal);
    }
  }

  bool isValid() const {
    return !sequences.empty();
  }

  std::string getSequenceString() const {
    return flattenSequences(sequences);
  }
};
