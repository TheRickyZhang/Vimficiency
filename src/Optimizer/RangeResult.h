#pragma once

#include <string>
#include <ostream>
#include <vector>

#include "Editor/Position.h"
#include "State/Sequence.h"
#include "Utils/StringUtils.h"

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

  friend std::ostream& operator<<(std::ostream& os, const RangeResult& r) {
    for(size_t i = 0; i < r.sequences.size(); i++) {
      const Sequence& s = r.sequences[i];
      if(i == 0 && s.mode == Mode::Insert) os << "I: ";
      if(i > 0) os << " ";
      os << makePrintable(s.keys);
    }
    os << " " << r.keyCost << " -> (" << r.endPos.line << "," << r.endPos.col << ")";
    return os;
  }
};
