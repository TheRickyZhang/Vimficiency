#pragma once

#include <string>
#include <vector>
#include <sstream>

#include "Editor/Mode.h"

// Represents a sequence of Vim commands/keys as a string, tagged with the mode
// it was executed in. Used by state classes to track the command history.
struct Sequence {
  std::string keys;
  Mode mode = Mode::Normal;

  Sequence() = default;
  Sequence(Mode m) : mode(m) {}
  Sequence(const std::string& k, Mode m) : keys(k), mode(m) {}
  Sequence(std::string&& k, Mode m) : keys(std::move(k)), mode(m) {}

  bool empty() const { return keys.empty(); }

  void append(const std::string& s) { keys += s; }
  void append(char c) { keys += c; }

  bool operator==(const Sequence& other) const {
    return keys == other.keys && mode == other.mode;
  }
  bool operator!=(const Sequence& other) const {
    return !(*this == other);
  }
};

// Helper to get flattened string from vector<Sequence>
inline std::string flattenSequences(const std::vector<Sequence>& seqs) {
  std::string result;
  for (const auto& s : seqs) {
    result += s.keys;
  }
  return result;
}

// Helper to get formatted string with mode annotations
inline std::string formatSequences(const std::vector<Sequence>& seqs) {
  std::ostringstream oss;
  for (size_t i = 0; i < seqs.size(); i++) {
    if (i > 0) oss << "\n";
    oss << (seqs[i].mode == Mode::Normal ? "Normal" : "Insert")
        << ": " << seqs[i].keys;
  }
  return oss.str();
}
