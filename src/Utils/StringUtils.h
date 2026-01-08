#pragma once

#include <string>

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
