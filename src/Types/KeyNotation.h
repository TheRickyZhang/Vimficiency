#pragma once

#include <optional>
#include <string>
#include <string_view>

// Vim key-notation helpers. Convention in this codebase: a `std::string` /
// `std::string_view` holding a Vim sequence is in notation form — `<Space>`,
// `<CR>`, `<lt>`, etc. Lua normalises raw vim.on_key bytes via `keytrans`
// before they cross the FFI; internal helpers produce notation by construction.
// `Line`/`Lines` is the type for buffer text (the other string category that
// would otherwise be conflated with notation).

// Notation for a single literal char.
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
