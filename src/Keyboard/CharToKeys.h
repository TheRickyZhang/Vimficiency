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
