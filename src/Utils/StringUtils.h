#pragma once

#include <string>
#include <string_view>

// Escape special characters for readable display (newlines -> \n, etc.)
inline std::string makePrintable(std::string_view s) {
  std::string result;
  result.reserve(s.size() * 2);
  for (char c : s) {
    switch (c) {
      case '\n': result += "\\n"; break;
      case '\t': result += "\\t"; break;
      case '\r': result += "\\r"; break;
      case '\\': result += "\\\\"; break;
      default: result += c; break;
    }
  }
  return result;
}

// Return the leading whitespace (spaces only) of a string
inline std::string_view leadingWhitespace(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && s[i] == ' ') i++;
  return s.substr(0, i);
}

// Count leading spaces in a string
inline int leadingSpaceCount(std::string_view s) {
  return static_cast<int>(leadingWhitespace(s).size());
}

// Compute number of <BS> presses to reduce indent from `from` to `to` spaces.
// In autoindent context, <BS> deletes to the previous shiftwidth boundary, not
// just 1 space. Returns -1 if <BS> overshoots past `to` (can't land exactly).
inline int bsCountForIndent(int from, int to, int sw) {
  int count = 0;
  int pos = from;
  while (pos > to) {
    pos = ((pos - 1) / sw) * sw;
    count++;
    if (pos < to) return -1;  // overshot
  }
  return count;
}

// Replace literal newlines with <CR> for use in Vim command sequences
inline std::string escapeNewlines(std::string_view s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    if (c == '\n') {
      result += "<CR>";
    } else {
      result += c;
    }
  }
  return result;
}
