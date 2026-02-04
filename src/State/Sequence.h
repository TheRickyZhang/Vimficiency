#pragma once

#include <ostream>
#include <string>

// Represents a sequence of Vim commands/keys as a string.
// Used by state classes to track the command history.
struct Sequence {
  std::string keys;

  Sequence() = default;
  Sequence(const std::string& k) : keys(k) {}
  Sequence(std::string&& k) : keys(std::move(k)) {}

  bool empty() const { return keys.empty(); }

  void append(const std::string& s) { keys += s; }
  void append(char c) { keys += c; }

  bool operator==(const Sequence& other) const {
    return keys == other.keys;
  }
  bool operator!=(const Sequence& other) const {
    return !(*this == other);
  }

  // Stream output with mode-separated spacing
  // Parses the sequence to identify mode transitions (insert entries via
  // i/I/a/A/o/O/s/S/C/R/c{motion}, insert exits via <Esc>) and inserts
  // spaces between segments.
  friend std::ostream& operator<<(std::ostream& os, const Sequence& seq);
};
