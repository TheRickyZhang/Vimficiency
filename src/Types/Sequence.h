#pragma once

#include <ostream>
#include <string>
#include <string_view>

// Represents a sequence of Vim commands/keys as a string.
// Used by state classes to track the command history.
class Sequence {
  std::string keys;
public:

  Sequence() = default;
  Sequence(const std::string& k) : keys(k) {}
  Sequence(std::string&& k) : keys(std::move(k)) {}

  bool empty() const { return keys.empty(); }
  size_t size() const { return keys.size(); }

  void append(char c, int count = 1) { keys.append(count, c); }
  void append(std::string_view s, int count = 1) {
    for (int i = 0; i < count; i++) keys += s;
  }

  bool operator==(const Sequence& other) const { return keys == other.keys; }
  bool operator!=(const Sequence& other) const { return !(*this == other); }
  bool operator==(const std::string& s) const { return keys == s; }
  bool operator!=(const std::string& s) const { return keys != s; }
  bool operator==(const char* s) const { return keys == s; }
  bool operator!=(const char* s) const { return keys != s; }
  bool operator<(const Sequence& other) const { return keys < other.keys; }

  // Preferred: non-owning view of the underlying string.
  std::string_view view() const { return keys; }

  // Use when const string& is needed (string concatenation with +,
  // APIs taking const string&, constructing owned copies).
  const std::string& str() const { return keys; }

  // Stream output with mode-separated spacing
  // Parses the sequence to identify mode transitions (insert entries via
  // i/I/a/A/o/O/s/S/C/R/c{motion}, insert exits via <Esc>) and inserts
  // spaces between segments.
  friend std::ostream& operator<<(std::ostream& os, const Sequence& seq);
};
