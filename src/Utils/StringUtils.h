#pragma once

#include <string>
#include <string_view>

// Escape special characters for readable display (newlines -> \n, etc.)
inline std::string makePrintable(const std::string& s) {
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
