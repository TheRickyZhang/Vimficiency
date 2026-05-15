// SequenceParser.h - Parse Vim command sequences into tokens for animation
// Handles motions, edit commands, and insert-mode typed content.
#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Types/Token.h"

// Parser tags for sequence tokens.
enum class TokenKind {
  Movement,    // Cursor movement (w, j, fa, ;, etc.)
  Delete,      // Pure deletion (dd, dw, x, X, D, etc.)
  Change,      // Edit that enters insert mode (ciw, s, A, etc.)
  Visual,      // Enters visual mode (v, V, <C-v>, gh, gH)
  TypedText,   // Text typed in insert mode
  Escape,      // <Esc> to exit insert (or visual) mode
};

struct TaggedToken {
  Token token;
  TokenKind kind;

  TaggedToken(Token t, TokenKind k) : token(std::move(t)), kind(k) {}
};

enum class SequenceParseErrorKind {
  UnknownCharacter,      // byte not recognized as motion/delete/change
  MalformedSpecialKey,   // '<...>' in command-mode slot isn't a known motion
};

struct SequenceParseError {
  SequenceParseErrorKind kind;
  size_t offset;
};

std::string formatSequenceParseError(const SequenceParseError& error);

// Parse a Vim command sequence into tagged tokens suitable for step-by-step animation.
// Example: "ciwhello<Esc>2j" -> [("ciw", Change), ("hello", TypedText), ("<Esc>", Escape), ("2j", Motion)]
//
// Insert-entering commands (recognized as Change type):
//   - Standalone: i, I, a, A, o, O, s, S, R
//   - Change operator: c{motion}, c{text-object}, C, cc
//
// Pure delete commands (recognized as Delete type):
//   - d{motion}, d{text-object}, D, dd, x, X, J, gJ
//
// After a Change token, everything until <Esc> is captured as TypedText.
//
// Fallible at the command-mode layer only. Insert-mode typed text is
// tolerant: a `<` without a closing `>` is treated as a literal char.
// The error channel exists so the Lua animation-fallback chain in
// simulate.lua can distinguish "parse failed" from "empty input".
std::expected<std::vector<TaggedToken>, SequenceParseError>
parseSequence(std::string_view seq);

// Simplified version that returns just the token strings (for FFI)
std::expected<std::vector<std::string>, SequenceParseError>
parseSequenceStrings(std::string_view seq);
