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
};

// Copy-on-write shared Lines for efficient state sharing in A* search.
// Motions share the same buffer (O(1)), edits copy-on-write (O(n)).
using SharedLines = std::shared_ptr<const Lines>;
