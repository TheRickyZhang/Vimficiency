#pragma once

#include <cassert>
#include <cstdint>

namespace VimCore {

struct CharMask {
  explicit constexpr CharMask(char c) : bits(classify(c)) {}

  // Character model:
  // CharMask represents an actual char, never a boundary sentinel.
  //
  //   char kind         blank  whitespace  newLine  bigWord  smallWord
  //   space/tab        yes    yes         no       no       no
  //   newline          yes    no          yes      no       no
  //   alnum/_          no     no          no       yes      yes
  //   punctuation      no     no          no       yes      no
  static constexpr bool isWhitespace(char c)     { return c == ' '        || c == '\t'; }
  static constexpr bool isBlank(char c)          { return isWhitespace(c) || c == '\n'; }
  static constexpr bool isNewline(char c)        { return c == '\n'; }
  static constexpr bool isSmallWord(char c)      { return isAlnumAscii(c) || c == '_'; }
  static constexpr bool isBigWord(char c)        { return !isBlank(c); }
  static constexpr bool isSentenceEnd(char c)    { return c == '.'        || c == '!' || c == '?'; }
  static constexpr bool isSentenceCloser(char c) { return c == ')'        || c == ']' || c == '"' || c == '\''; }

  constexpr bool whitespace() const     { return has(Whitespace); }
  constexpr bool blank() const          { return has(Blank); }
  constexpr bool newLine() const        { return blank() && !whitespace(); }
  constexpr bool smallWord() const      { return has(SmallWord); }
  constexpr bool bigWord() const        { return !blank(); }
  constexpr bool sentenceEnd() const    { return has(SentenceEnd); }
  constexpr bool sentenceCloser() const { return has(SentenceCloser); }


private:
  enum CharBit : uint8_t {
    Whitespace     = 1 << 0,
    Blank          = 1 << 1,
    SmallWord      = 1 << 2,
    SentenceEnd    = 1 << 3,
    SentenceCloser = 1 << 4,
  };

  uint8_t bits = 0;

  constexpr bool has(CharBit bit) const {
    return (bits & static_cast<uint8_t>(bit)) != 0;
  }

  // Vim word chars here are ASCII-only; this avoids std::isalnum's locale and unsigned-char preconditions.
  static constexpr bool isAlnumAscii(char c) {
    return ('a' <= c && c <= 'z') ||
           ('A' <= c && c <= 'Z') ||
           ('0' <= c && c <= '9');
  }

  static constexpr uint8_t classify(char c) {
    if (isWhitespace(c)) return Whitespace | Blank;
    if (isNewline(c)) return Blank;

    uint8_t result = 0;
    if (isSmallWord(c)) result |= SmallWord;
    if (isSentenceEnd(c)) result |= SentenceEnd;
    if (isSentenceCloser(c)) result |= SentenceCloser;
    return result;
  }
};

// Core spans are the strict interpretation that only considers nonblank chars, like the shared base of w/e before any space handling.
constexpr bool sameWordSpan(CharMask a, CharMask b) {
  return !a.blank() && !b.blank() && a.smallWord() == b.smallWord();
}

constexpr bool sameBigWordSpan(CharMask a, CharMask b) {
  return !a.blank() && !b.blank();
}

constexpr bool sameSpan(CharMask a, CharMask b, bool big) {
  return big ? sameBigWordSpan(a, b) : sameWordSpan(a, b);
}

// Scan continuation uses that same strict interpretation, with the invariant that spanChar is already inside a core span.
constexpr bool continuesWordSpan(CharMask a, CharMask b) {
  assert(!a.blank());
  return !b.blank() && a.smallWord() == b.smallWord();
}

constexpr bool continuesBigWordSpan(CharMask a, CharMask b) {
  assert(!a.blank());
  return !b.blank();
}

constexpr bool continuesSpan(CharMask spanChar, CharMask curr, bool big) {
  return big ? continuesBigWordSpan(spanChar, curr)
             : continuesWordSpan(spanChar, curr);
}

// Broad = more general interpretation that includes blank chars after a word as part of the same word
// Currently used by VimDiff's delete-cost oracle.
constexpr bool beginsWordBroad(CharMask prev, CharMask curr) {
  return !curr.blank() && !sameWordSpan(prev, curr);
}

constexpr bool endsWordBroad(CharMask curr, CharMask next) {
  return !curr.blank() && !sameWordSpan(curr, next);
}

constexpr bool beginsBigWordBroad(CharMask prev, CharMask curr) {
  return !curr.blank() && !sameBigWordSpan(prev, curr);
}

constexpr bool endsBigWordBroad(CharMask curr, CharMask next) {
  return !curr.blank() && !sameBigWordSpan(curr, next);
}

} // namespace VimCore
