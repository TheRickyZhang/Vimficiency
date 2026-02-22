// SequenceParser.h - Parse Vim command sequences into tokens for animation
// Handles motions, edit commands, and insert-mode typed content.
#pragma once

#include <string>
#include <string_view>
#include <vector>

// Token types for parsed sequences
enum class TokenType {
  Motion,      // Cursor movement (w, j, fa;, etc.)
  Delete,      // Pure deletion (dd, dw, x, X, D, etc.)
  Change,      // Edit that enters insert mode (ciw, s, A, etc.)
  TypedText,   // Text typed in insert mode
  Escape,      // <Esc> to exit insert mode
};

struct SequenceToken {
  std::string text;
  TokenType type;

  SequenceToken(std::string t, TokenType ty) : text(std::move(t)), type(ty) {}
};

// Parse a Vim command sequence into tokens suitable for step-by-step animation.
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
std::vector<SequenceToken> parseSequence(std::string_view seq);

// Simplified version that returns just the token strings (for FFI)
std::vector<std::string> parseSequenceStrings(std::string_view seq);
