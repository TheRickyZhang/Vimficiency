#pragma once

#include <unordered_map>
#include "KeyboardModel.h"

// Single-character to PhysicalKeys mapping
using CharToKeys = std::unordered_map<char, PhysicalKeys>;

// All printable characters mapped to their key sequences
extern const CharToKeys CHAR_TO_KEYS;

// Building blocks for character categories
namespace CharMappings {

// Letters (a-z, A-Z)
extern const CharToKeys letters;

// Digits (0-9)
extern const CharToKeys digits;

const std::array<Key, 10> digitsArr = {
  Key::Key_0,
  Key::Key_1,
  Key::Key_2,
  Key::Key_3,
  Key::Key_4,
  Key::Key_5,
  Key::Key_6,
  Key::Key_7,
  Key::Key_8,
  Key::Key_9,
};

// Whitespace (space, tab, newline)
extern const CharToKeys whitespace;

// Punctuation from top row (`, ~, -, _, =, +, [, {, ], }, \, |)
extern const CharToKeys topPunctuation;

// Main punctuation (; : ' " , < . > / ?)
extern const CharToKeys mainPunctuation;

// Symbols above digits (! @ # $ % ^ & * ( ))
extern const CharToKeys digitSymbols;

// All punctuation and symbols combined
extern const CharToKeys allPunctuationAndSymbols;

} // namespace CharMappings
