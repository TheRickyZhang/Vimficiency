#pragma once

#include <ostream>
#include <string>
#include <string_view>

namespace VF {

// Text with whitespace/control chars rendered as visible glyphs.
struct PrettyText {
  static constexpr std::string_view SPACE = "\xE2\x90\xA3";  // ␣ U+2423
  static constexpr std::string_view TAB = "\xE2\x87\xA5";  // ⇥ U+21E5
  static constexpr std::string_view NEWLINE = "\xE2\x86\xB5";  // ↵ U+21B5
  static constexpr std::string_view TREE_DIVIDER = "\xE2\x94\x83";  // ┃ U+2503
  static constexpr std::string_view TREE_OPEN = "\xE2\x9F\xA6";  // ⟦ U+27E6
  static constexpr std::string_view TREE_CLOSE = "\xE2\x9F\xA7";  // ⟧ U+27E7
  static constexpr std::string_view REMOVE = "\xC3\x97";  // × U+00D7
  static constexpr std::string_view ARROW = "\xE2\x86\x92";  // → U+2192

  std::string text;

  PrettyText() = default;
  explicit PrettyText(char c) { appendGlyph(c); }
  explicit PrettyText(std::string_view source) {
    text.reserve(source.size());
    for (const char c : source) appendGlyph(c);
  }

  friend std::ostream& operator<<(std::ostream& out, const PrettyText& p) {
    return out << p.text;
  }

private:
  void appendGlyph(char c) {
    switch (c) {
      case ' ':  text += SPACE; break;
      case '\t': text += TAB; break;
      case '\n': text += NEWLINE; break;
      default:   text += c; break;
    }
  }
};

inline std::string prettify(char c) { return PrettyText(c).text; }
inline std::string prettify(std::string_view source) { return PrettyText(source).text; }

}  // namespace VF
