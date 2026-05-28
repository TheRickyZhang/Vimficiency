#pragma once

#include <optional>
#include <string>
#include <string_view>

// Vim key notation for special chars (whitespace, `<`). One char in,
// either its literal form or a `<Space>`/`<Tab>`/`<CR>`/`<lt>` token.
inline std::string displayChar(char c) {
  switch (c) {
    case ' ':  return "<Space>";
    case '\t': return "<Tab>";
    case '\n':
    case '\r': return "<CR>";
    case '<':  return "<lt>";
    default:   return std::string(1, c);
  }
}

inline std::optional<char> parseDisplayChar(std::string_view s) {
  if (s.size() == 1) return s[0];
  if (s == "<Space>") return ' ';
  if (s == "<Tab>")   return '\t';
  if (s == "<CR>")    return '\n';
  if (s == "<lt>" || s == "<LT>") return '<';
  return std::nullopt;
}
