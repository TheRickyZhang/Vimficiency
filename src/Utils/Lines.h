#pragma once

#include <memory>
#include <string>
#include <vector>

struct Lines : std::vector<std::string> {
  using std::vector<std::string>::vector;

  std::string flatten() const {
    std::string result;
    for (size_t i = 0; i < size(); i++) {
      if (i > 0)
        result += '\n';
      result += (*this)[i];
    }
    return result;
  }

  static Lines unflatten(const std::string& text) {
    Lines result;
    size_t start = 0;
    size_t pos;
    while ((pos = text.find('\n', start)) != std::string::npos) {
      result.push_back(text.substr(start, pos - start));
      start = pos + 1;
    }
    result.push_back(text.substr(start));
    return result;
  }

  // Total character count (excluding newlines between lines)
  int charCount() const {
    int count = 0;
    for (const auto& line : *this) {
      count += static_cast<int>(line.size());
    }
    return count;
  }
};

// Copy-on-write shared Lines for efficient state sharing in A* search.
// Motions share the same buffer (O(1)), edits copy-on-write (O(n)).
using SharedLines = std::shared_ptr<const Lines>;
