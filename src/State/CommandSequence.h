#pragma once

#include <ostream>
#include <string>
#include <vector>

// Forward declaration
std::vector<std::string> parseSequenceStrings(const std::string& seq);

// CommandSequence extends std::string with formatted output for display.
//
// Internally stores the raw Vim command string for execution.
// Provides formatted() method that tokenizes into logical units for display.
//
// Example:
//   CommandSequence seq("3rx<C-d>ciwfoo<Esc>");
//   seq == "3rx<C-d>ciwfoo<Esc>"    // Raw string for execution
//   seq.formatted() == "3rx <C-d> ciw foo <Esc>"  // Tokenized for display
struct CommandSequence : std::string {
  using std::string::string;

  // Allow conversion from std::string
  CommandSequence(const std::string& s) : std::string(s) {}
  CommandSequence(std::string&& s) : std::string(std::move(s)) {}

  // Format for human-readable display: tokenize and join with spaces
  std::string formatted() const;

  // Stream output uses formatted version for readability
  friend std::ostream& operator<<(std::ostream& os, const CommandSequence& cs) {
    return os << cs.formatted();
  }
};

// Utility function to format any sequence string (for use without CommandSequence)
std::string formatSequenceForDisplay(const std::string& seq);
