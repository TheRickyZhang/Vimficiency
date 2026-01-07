#include "Sequence.h"

std::string flattenSequences(const std::vector<Sequence>& seqs) {
  size_t total = 0;
  for (const auto& s : seqs) {
    total += s.keys.size();
  }
  std::string result;
  result.reserve(total);
  for (const auto& s : seqs) {
    result += s.keys;
  }
  return result;
}

